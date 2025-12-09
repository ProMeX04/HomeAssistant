#!/usr/bin/env python3
"""
JARVIS v6 - VAD-Only Mode Server

Features:
1. Continuous audio streaming (no wake word needed)
2. Server-side VAD (Voice Activity Detection) - auto detect speech
3. Voice Interrupt (detect speech during AI playback)

ESP32 streams audio continuously, server uses VAD to detect when user speaks.
"""

import asyncio
import logging
import os
import sys
import tempfile
from datetime import datetime
from collections import deque

import aiohttp
import numpy as np
import torch
import websockets

logging.basicConfig(
    level=logging.DEBUG,  # Changed to DEBUG for more logs
    format="%(asctime)s [%(levelname)s] %(message)s",
    stream=sys.stdout,
    force=True,
)
logger = logging.getLogger(__name__)

# Debug counter - log every N chunks to avoid spam
DEBUG_LOG_INTERVAL = 20  # Log audio stats every 20 chunks (~2.5 sec)

# ============================================================================
# Configuration
# ============================================================================
PORT = 6666
HTTP_PORT = 6667
N8N_WEBHOOK_URL = "http://localhost:5678/webhook/753a58bd-1b12-4643-858f-9249c3477da5"

# Audio settings
SAMPLE_RATE = 16000
CHUNK_DURATION_MS = 128  # ~2KB chunk @ 16kHz/16bit
SAMPLES_PER_CHUNK = int(SAMPLE_RATE * CHUNK_DURATION_MS / 1000)

# VAD settings (no wake word needed)
VAD_THRESHOLD = 0.4
MIN_SPEECH_SEC = 0.3  # Minimum speech duration before processing
SILENCE_TIMEOUT_SEC = 1.5  # Silence duration to end recording
MAX_RECORDING_SEC = 15  # Maximum recording duration
SPEECH_START_CHUNKS = 3  # Consecutive speech chunks to start recording

# Software gain - amplify weak mic signal from ESP32
# ESP32-LyraT mic is weak without AGC, multiply to boost signal
SOFTWARE_GAIN = 20.0  # Increase this if audio still too quiet (try 10-50)

# Debug: Save audio to file for inspection
DEBUG_SAVE_AUDIO = True  # Set to True to save audio files
DEBUG_AUDIO_DIR = "debug_audio"  # Directory to save audio files
DEBUG_AUDIO_SECONDS = 10  # Save every N seconds of audio

# Voice Interrupt settings
VOICE_INTERRUPT_THRESHOLD = 0.5  # Higher threshold during playback
VOICE_INTERRUPT_CHUNKS = 3  # Consecutive voice chunks to trigger interrupt

# ============================================================================
# Load Models
# ============================================================================
logger.info("Loading Silero VAD...")
try:
    vad_model, vad_utils = torch.hub.load(
        repo_or_dir="snakers4/silero-vad",
        model="silero_vad",
        force_reload=False,
        onnx=False,
    )
    get_speech_timestamps, _, _, _, _ = vad_utils
    VAD_AVAILABLE = True
    logger.info("✅ Silero VAD loaded")
except Exception as e:
    logger.warning(f"⚠️ Silero VAD not available: {e}")
    VAD_AVAILABLE = False

# No wake word detection needed - VAD only


# ============================================================================
# State Management
# ============================================================================
class ClientState:
    """State machine for each client - VAD only, no wake word"""
    
    STATE_IDLE = "idle"           # Waiting for speech
    STATE_LISTENING = "listening"  # Collecting speech (VAD detected)
    STATE_PROCESSING = "processing"  # Processing with n8n
    STATE_PLAYING = "playing"      # Playing AI response
    
    def __init__(self):
        self.state = self.STATE_IDLE
        self.recording_buffer = []
        self.recording_start = None
        self.last_speech_time = None
        self.speech_chunk_count = 0  # Consecutive speech chunks
        self.voice_interrupt_count = 0
        self.current_task = None
        self.debug_counter = 0  # For debug logging
        self.total_chunks = 0   # Total audio chunks received
        
        # Debug audio saving
        self.debug_audio_buffer = []  # Buffer for debug audio (raw int16)
        self.debug_save_counter = 0
        
    def reset_recording(self):
        self.recording_buffer = []
        self.recording_start = None
        self.last_speech_time = None
        self.speech_chunk_count = 0
        self.voice_interrupt_count = 0


# ============================================================================
# Debug Audio Saving
# ============================================================================
def save_debug_audio(client_state):
    """Save debug audio buffer to WAV file for inspection"""
    import soundfile as sf
    
    if not client_state.debug_audio_buffer:
        return
    
    # Create debug directory if not exists
    os.makedirs(DEBUG_AUDIO_DIR, exist_ok=True)
    
    # Concatenate all chunks
    audio_data = np.concatenate(client_state.debug_audio_buffer)
    
    # Generate filename with timestamp
    client_state.debug_save_counter += 1
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"{DEBUG_AUDIO_DIR}/debug_{timestamp}_{client_state.debug_save_counter:03d}.wav"
    
    # Calculate stats
    duration = len(audio_data) / SAMPLE_RATE
    raw_max = np.abs(audio_data).max()
    rms = np.sqrt(np.mean(audio_data.astype(np.float32) ** 2))
    
    # Save as WAV (16-bit PCM)
    sf.write(filename, audio_data, SAMPLE_RATE)
    
    logger.info(f"💾 Saved debug audio: {filename}")
    logger.info(f"   Duration: {duration:.1f}s, Samples: {len(audio_data)}, Max: {raw_max}, RMS: {rms:.1f}")
    
    # Clear buffer
    client_state.debug_audio_buffer = []


# ============================================================================
# Audio Analysis Functions
# ============================================================================
# VAD buffer for accumulating samples (Silero requires exactly 512 samples at 16kHz)
VAD_CHUNK_SIZE = 512
vad_sample_buffer = []

def get_speech_probability(audio_chunk):
    """Get speech probability for audio chunk using VAD
    
    Silero VAD requires exactly 512 samples at 16kHz.
    We accumulate samples and process when we have enough.
    """
    global vad_sample_buffer
    
    if not VAD_AVAILABLE:
        return 0.5  # Assume speech if VAD not available
    
    try:
        # Add new samples to buffer
        vad_sample_buffer.extend(audio_chunk.tolist())
        
        # Process if we have enough samples
        if len(vad_sample_buffer) >= VAD_CHUNK_SIZE:
            # Take exactly 512 samples
            samples = np.array(vad_sample_buffer[:VAD_CHUNK_SIZE], dtype=np.float32)
            # Keep remaining samples for next call
            vad_sample_buffer = vad_sample_buffer[VAD_CHUNK_SIZE:]
            
            # Debug: Check sample stats before VAD
            sample_max = np.abs(samples).max()
            sample_rms = np.sqrt(np.mean(samples ** 2))
            
            audio_tensor = torch.from_numpy(samples)
            prob = vad_model(audio_tensor, SAMPLE_RATE).item()
            
            # Log occasionally for debugging
            if prob > 0.1 or sample_max > 0.01:
                logger.debug(f"VAD: prob={prob:.3f}, sample_max={sample_max:.4f}, rms={sample_rms:.4f}")
            
            return prob
        else:
            # Not enough samples yet, return neutral
            return 0.3
            
    except Exception as e:
        logger.error(f"VAD error: {e}")
        vad_sample_buffer = []  # Reset on error
        return 0.5


# Wake word detection removed - using VAD only


def trim_silence(audio_data, sample_rate=16000):
    """Trim silence from audio"""
    if not VAD_AVAILABLE:
        return audio_data
    
    try:
        audio_tensor = torch.from_numpy(audio_data).float()
        speech_timestamps = get_speech_timestamps(
            audio_tensor,
            vad_model,
            sampling_rate=sample_rate,
            threshold=VAD_THRESHOLD,
            min_speech_duration_ms=100,
            min_silence_duration_ms=150,
        )
        
        if not speech_timestamps:
            return audio_data
        
        start = max(0, speech_timestamps[0]["start"] - int(0.1 * sample_rate))
        end = min(len(audio_data), speech_timestamps[-1]["end"] + int(0.1 * sample_rate))
        
        return audio_data[start:end]
    except Exception as e:
        logger.error(f"Trim error: {e}")
        return audio_data


# ============================================================================
# Text-to-Speech
# ============================================================================
def is_vietnamese(text):
    vn_chars = set("àáạảãâầấậẩẫăằắặẳẵèéẹẻẽêềếệểễìíịỉĩòóọỏõôồốộổỗơờớợởỡùúụủũưừứựửữỳýỵỷỹđ")
    return any(c in vn_chars for c in text.lower())


async def tts_stream(text):
    """Convert text to MP3 speech"""
    if not text or not text.strip():
        return
    
    voice = "Linh" if is_vietnamese(text) else "Samantha"
    logger.info(f"🗣️ TTS ({voice}): '{text[:40]}...'")
    
    temp_file = None
    try:
        with tempfile.NamedTemporaryFile(suffix=".aiff", delete=False) as f:
            temp_file = f.name
        
        proc = await asyncio.create_subprocess_exec(
            "say", "-v", voice, "-o", temp_file, text,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL,
        )
        await proc.wait()
        
        filters = ["apad=pad_dur=0.3"]
        if voice == "Linh":
            filters.insert(0, "atempo=1.25")
        
        ffmpeg = await asyncio.create_subprocess_exec(
            "ffmpeg", "-i", temp_file,
            "-f", "mp3", "-ac", "1", "-ar", "44100", "-b:a", "128k",
            "-filter:a", ",".join(filters),
            "pipe:1",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        
        while True:
            chunk = await ffmpeg.stdout.read(8192)
            if not chunk:
                break
            yield chunk
        
        await ffmpeg.wait()
        
    except asyncio.CancelledError:
        raise
    except Exception as e:
        logger.error(f"TTS error: {e}")
    finally:
        if temp_file and os.path.exists(temp_file):
            try:
                os.remove(temp_file)
            except:
                pass


# ============================================================================
# n8n Integration
# ============================================================================
async def call_n8n(audio_path):
    """Call n8n with audio - returns MP3 bytes"""
    async with aiohttp.ClientSession() as session:
        try:
            logger.info(f"📤 Sending to n8n: {audio_path}")
            
            with open(audio_path, "rb") as f:
                data = aiohttp.FormData()
                data.add_field("file", f, filename="audio.wav", content_type="audio/wav")
                
                async with session.post(N8N_WEBHOOK_URL, data=data, timeout=45) as resp:
                    if resp.status == 200:
                        audio_data = await resp.read()
                        logger.info(f"🎵 Received {len(audio_data)} bytes from n8n")
                        return audio_data
                    else:
                        logger.error(f"n8n error: {resp.status}")
                        return None
                        
        except Exception as e:
            logger.error(f"n8n error: {e}")
            return None


# ============================================================================
# Process Pipeline
# ============================================================================
async def process_audio(audio_buffer, websocket, client_state):
    """Process recorded audio: trim → n8n → stream response"""
    import soundfile as sf
    
    try:
        client_state.state = ClientState.STATE_PROCESSING
        
        audio_data = np.concatenate(audio_buffer)
        duration = len(audio_data) / SAMPLE_RATE
        logger.info(f"Processing {duration:.2f}s audio")
        
        if duration < MIN_SPEECH_SEC:
            logger.warning("Audio too short, ignoring")
            client_state.state = ClientState.STATE_IDLE
            return
        
        # Trim silence
        trimmed = trim_silence(audio_data)
        
        # Save temp file
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            temp_path = f.name
            sf.write(temp_path, trimmed, SAMPLE_RATE)
        
        # Signal start
        await websocket.send("AUDIO_START")
        client_state.state = ClientState.STATE_PLAYING
        
        # Get response from n8n
        audio_bytes = await call_n8n(temp_path)
        os.remove(temp_path)
        
        # Stream response
        if audio_bytes:
            logger.info(f"🔊 Streaming {len(audio_bytes)} bytes")
            
            chunk_size = 8192
            for i in range(0, len(audio_bytes), chunk_size):
                # Check for voice interrupt
                if client_state.state != ClientState.STATE_PLAYING:
                    logger.warning("⏹️ Playback interrupted!")
                    break
                    
                chunk = audio_bytes[i:i + chunk_size]
                await websocket.send(chunk)
                await asyncio.sleep(0.01)  # Small delay for ESP32
            
            logger.info("✅ Response streamed")
        else:
            logger.error("❌ No response from n8n")
        
        await asyncio.sleep(0.3)
        await websocket.send("AUDIO_END")
        logger.info("✅ AUDIO_END sent")
        
    except asyncio.CancelledError:
        logger.info("Pipeline cancelled")
        raise
    except Exception as e:
        logger.error(f"Pipeline error: {e}")
    finally:
        client_state.state = ClientState.STATE_IDLE


# ============================================================================
# WebSocket Handler - Baidu RTC Style
# ============================================================================
active_ws = None
active_state = None


async def handle_client(websocket):
    """Handle ESP32 client with continuous streaming"""
    global active_ws, active_state, vad_sample_buffer
    
    # Reset VAD buffer for new client
    vad_sample_buffer = []
    
    active_ws = websocket
    client_state = ClientState()
    active_state = client_state
    
    client_addr = websocket.remote_address
    logger.info(f"🔌 Connected: {client_addr}")
    logger.info("📡 Continuous streaming mode - VAD only (no wake word)")
    
    try:
        async for message in websocket:
            current_time = asyncio.get_event_loop().time()
            
            # Text commands (from ESP32)
            if isinstance(message, str):
                logger.debug(f"Text: {message}")
                continue
            
            # Binary audio - continuous stream
            raw_chunk = np.frombuffer(message, dtype=np.int16)
            
            # Debug: Save raw audio to file for inspection
            if DEBUG_SAVE_AUDIO:
                client_state.debug_audio_buffer.append(raw_chunk.copy())
                # Check if we have enough audio to save
                total_samples = sum(len(c) for c in client_state.debug_audio_buffer)
                if total_samples >= SAMPLE_RATE * DEBUG_AUDIO_SECONDS:
                    save_debug_audio(client_state)
            
            # Apply software gain to boost weak mic signal
            chunk = raw_chunk.astype(np.float32) / 32768.0
            chunk = chunk * SOFTWARE_GAIN
            chunk = np.clip(chunk, -1.0, 1.0)  # Prevent clipping
            
            client_state.total_chunks += 1
            client_state.debug_counter += 1
            
            # Log first chunk to confirm audio is being received
            if client_state.total_chunks == 1:
                logger.info(f"🎵 First audio chunk received! size={len(chunk)} samples")
                logger.info(f"📊 Raw int16 range: min={raw_chunk.min()}, max={raw_chunk.max()}")
                logger.info(f"🔊 Software gain: {SOFTWARE_GAIN}x applied")
                if DEBUG_SAVE_AUDIO:
                    logger.info(f"💾 Debug audio saving ENABLED - files in '{DEBUG_AUDIO_DIR}/' every {DEBUG_AUDIO_SECONDS}s")
            
            # Calculate audio stats for debugging (use raw values for clarity)
            raw_max = np.abs(raw_chunk).max()
            boosted_max = np.abs(chunk).max()
            rms = np.sqrt(np.mean(chunk ** 2))
            
            # Get speech probability
            speech_prob = get_speech_probability(chunk)
            is_speech = speech_prob > VAD_THRESHOLD
            
            # Debug logging every N chunks
            if client_state.debug_counter >= DEBUG_LOG_INTERVAL:
                client_state.debug_counter = 0
                logger.info(
                    f"🔊 Audio: chunks={client_state.total_chunks}, "
                    f"raw={raw_max}, boosted={boosted_max:.3f}, "
                    f"VAD={speech_prob:.2f}, speech={is_speech}, "
                    f"state={client_state.state}"
                )
                if raw_max < 50:
                    logger.warning(f"⚠️ Audio level LOW! raw_max={raw_max} (after {SOFTWARE_GAIN}x gain = {boosted_max:.3f})")
            
            # === STATE: IDLE - Looking for speech (VAD only) ===
            if client_state.state == ClientState.STATE_IDLE:
                if is_speech:
                    # Count consecutive speech chunks
                    client_state.speech_chunk_count += 1
                    client_state.recording_buffer.append(chunk)
                    logger.debug(f"🗣️ Speech chunk {client_state.speech_chunk_count}/{SPEECH_START_CHUNKS}, VAD={speech_prob:.2f}")
                    
                    # Start recording after N consecutive speech chunks
                    if client_state.speech_chunk_count >= SPEECH_START_CHUNKS:
                        logger.info(f"🎤 Speech detected! Starting recording... (VAD={speech_prob:.2f})")
                        client_state.state = ClientState.STATE_LISTENING
                        client_state.recording_start = current_time
                        client_state.last_speech_time = current_time
                else:
                    # Silence - reset speech counter
                    if client_state.speech_chunk_count > 0:
                        logger.debug(f"🔇 Speech interrupted, resetting count (was {client_state.speech_chunk_count})")
                    client_state.speech_chunk_count = 0
                    # Keep small buffer for smooth transition
                    if client_state.recording_buffer:
                        client_state.recording_buffer = client_state.recording_buffer[-3:]
            
            # === STATE: LISTENING - Collecting speech ===
            elif client_state.state == ClientState.STATE_LISTENING:
                client_state.recording_buffer.append(chunk)
                
                if is_speech:
                    client_state.last_speech_time = current_time
                    client_state.speech_chunk_count = 0  # Reset counter
                
                # Check for end conditions
                time_since_speech = current_time - (client_state.last_speech_time or current_time)
                recording_duration = current_time - client_state.recording_start
                
                # Log recording progress
                if client_state.debug_counter == 0:  # Same interval as audio debug
                    logger.debug(
                        f"📝 Recording: {recording_duration:.1f}s, "
                        f"silence={time_since_speech:.1f}s, "
                        f"buffers={len(client_state.recording_buffer)}"
                    )
                
                # End on silence timeout
                if time_since_speech > SILENCE_TIMEOUT_SEC:
                    logger.info(f"🔇 Silence detected ({time_since_speech:.1f}s)")
                    
                    # Process the recording
                    if client_state.recording_buffer:
                        if client_state.current_task and not client_state.current_task.done():
                            client_state.current_task.cancel()
                        
                        client_state.current_task = asyncio.create_task(
                            process_audio(list(client_state.recording_buffer), websocket, client_state)
                        )
                    
                    client_state.reset_recording()
                
                # End on max duration
                elif recording_duration > MAX_RECORDING_SEC:
                    logger.warning("⏱️ Max recording duration")
                    
                    if client_state.recording_buffer:
                        if client_state.current_task and not client_state.current_task.done():
                            client_state.current_task.cancel()
                        
                        client_state.current_task = asyncio.create_task(
                            process_audio(list(client_state.recording_buffer), websocket, client_state)
                        )
                    
                    client_state.reset_recording()
            
            # === STATE: PLAYING - Voice Interrupt Detection ===
            elif client_state.state == ClientState.STATE_PLAYING:
                # Check for voice interrupt (user speaking during playback)
                if speech_prob > VOICE_INTERRUPT_THRESHOLD:
                    client_state.voice_interrupt_count += 1
                    
                    if client_state.voice_interrupt_count >= VOICE_INTERRUPT_CHUNKS:
                        logger.warning("🔇 VOICE INTERRUPT detected!")
                        
                        # Cancel current playback
                        if client_state.current_task and not client_state.current_task.done():
                            client_state.current_task.cancel()
                        
                        # Switch to listening mode
                        client_state.state = ClientState.STATE_LISTENING
                        client_state.recording_buffer = [chunk]
                        client_state.recording_start = current_time
                        client_state.last_speech_time = current_time
                        client_state.voice_interrupt_count = 0
                        
                        # Notify ESP32
                        try:
                            await websocket.send("AUDIO_END")
                        except:
                            pass
                else:
                    client_state.voice_interrupt_count = 0
    
    except websockets.exceptions.ConnectionClosed:
        logger.info(f"Connection closed: {client_addr}")
    except Exception as e:
        logger.error(f"Handler error: {e}")
    finally:
        if client_state.current_task and not client_state.current_task.done():
            client_state.current_task.cancel()
        
        if active_ws == websocket:
            active_ws = None
            active_state = None
        
        logger.info(f"🔌 Disconnected: {client_addr}")


# ============================================================================
# HTTP API
# ============================================================================
async def handle_speak_request(request):
    """Immediate TTS to ESP32"""
    try:
        data = await request.json()
        text = data.get("text", "")
        
        if not active_ws:
            return web.json_response({"error": "No client"}, status=400)
        
        if not text:
            return web.json_response({"error": "Missing text"}, status=400)
        
        logger.info(f"📢 Speaking: '{text[:40]}...'")
        
        if active_state:
            active_state.state = ClientState.STATE_PLAYING
        
        await active_ws.send("AUDIO_START")
        async for chunk in tts_stream(text):
            await active_ws.send(chunk)
        await asyncio.sleep(0.3)
        await active_ws.send("AUDIO_END")
        
        if active_state:
            active_state.state = ClientState.STATE_IDLE
        
        return web.json_response({"status": "ok"})
        
    except Exception as e:
        return web.json_response({"error": str(e)}, status=500)


async def handle_status(request):
    """Server status endpoint"""
    return web.json_response({
        "status": "running",
        "mode": "vad_only",
        "client_connected": active_ws is not None,
        "client_state": active_state.state if active_state else None,
        "vad_available": VAD_AVAILABLE,
    })


async def start_http_server():
    """Start HTTP API server"""
    app = web.Application()
    app.router.add_post("/speak", handle_speak_request)
    app.router.add_get("/status", handle_status)
    
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "0.0.0.0", HTTP_PORT)
    await site.start()
    
    logger.info(f"🌐 HTTP API on port {HTTP_PORT}")


# ============================================================================
# Main
# ============================================================================
async def main():
    logger.info("╔════════════════════════════════════════╗")
    logger.info("║   JARVIS v6 Server - VAD Only Mode   ║")
    logger.info("║   Continuous Streaming (No Wake Word)  ║")
    logger.info("╠════════════════════════════════════════╣")
    logger.info(f"║   VAD:     {'✅ Ready' if VAD_AVAILABLE else '❌ Not available':<20} ║")
    logger.info("╠════════════════════════════════════════╣")
    logger.info("║   Features:                           ║")
    logger.info("║   • Auto-detect speech (VAD)          ║")
    logger.info("║   • No wake word needed               ║")
    logger.info("║   • Voice Interrupt Detection         ║")
    logger.info("╚════════════════════════════════════════╝")
    
    await start_http_server()
    
    async with websockets.serve(
        handle_client, 
        "0.0.0.0", 
        PORT, 
        max_size=64 * 1024,
        ping_interval=20,
        ping_timeout=10,
    ):
        logger.info(f"🎤 WebSocket server on port {PORT}")
        logger.info("   Waiting for ESP32 (continuous stream)...")
        await asyncio.Future()


if __name__ == "__main__":
    try:
        from aiohttp import web
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Server stopped")
