#!/usr/bin/env python3
"""
JARVIS v4 - Ultra-Optimized Streaming Server

Optimizations for ESP32-LyraT-Mini:
1. Smart VAD that works with on-device VAD (backup/verification)
2. Faster audio processing with streaming
3. Reduced memory footprint
4. Better error handling
"""

import asyncio
import logging
import os
import sys
import tempfile
from datetime import datetime

import aiohttp
import numpy as np
import torch
import websockets

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    stream=sys.stdout,
    force=True,
)
logger = logging.getLogger(__name__)

# ============================================================================
# Configuration - Tuned for ESP32-LyraT-Mini
# ============================================================================
PORT = 6666
HTTP_PORT = 6667
N8N_WEBHOOK_URL = "http://localhost:5678/webhook/753a58bd-1b12-4643-858f-9249c3477da5"

# VAD Settings - Relaxed since device does primary VAD
SILENCE_CHUNKS = 4           # Fewer - device already filtered
MIN_RECORDING_CHUNKS = 4     # Minimum chunks before processing
VAD_THRESHOLD = 0.35         # Lower threshold
MAX_RECORDING_SEC = 15       # Maximum recording duration

# Audio settings
SAMPLE_RATE = 16000
CHUNK_DURATION_MS = 128      # ~2KB chunk @ 16kHz/16bit

# ============================================================================
# Load Silero VAD (once at startup)
# ============================================================================
logger.info("Loading Silero VAD (backup mode)...")
try:
    vad_model, vad_utils = torch.hub.load(
        repo_or_dir="snakers4/silero-vad",
        model="silero_vad",
        force_reload=False,
        onnx=False,
    )
    get_speech_timestamps = vad_utils[0]
    VAD_AVAILABLE = True
    logger.info("✅ Silero VAD loaded")
except Exception as e:
    logger.warning(f"⚠️ Silero VAD not available: {e}")
    VAD_AVAILABLE = False


# ============================================================================
# Audio Processing Functions
# ============================================================================
def trim_silence_fast(audio_data, sample_rate=16000):
    """Fast silence trimming - lightweight for backup VAD"""
    if not VAD_AVAILABLE:
        return audio_data
    
    try:
        # Quick RMS-based pre-filter
        rms = np.sqrt(np.mean(audio_data ** 2))
        if rms < 0.01:  # Very quiet
            logger.warning("Audio too quiet")
            return audio_data
        
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
            logger.warning("No speech in audio")
            return audio_data
        
        start = max(0, speech_timestamps[0]["start"] - int(0.1 * sample_rate))  # 100ms padding
        end = min(len(audio_data), speech_timestamps[-1]["end"] + int(0.1 * sample_rate))
        
        trimmed = audio_data[start:end]
        logger.info(f"Trimmed {len(audio_data)/sample_rate:.2f}s → {len(trimmed)/sample_rate:.2f}s")
        
        return trimmed
    except Exception as e:
        logger.error(f"Trim error: {e}")
        return audio_data


def is_vietnamese(text):
    """Quick Vietnamese detection"""
    vn_chars = set("àáạảãâầấậẩẫăằắặẳẵèéẹẻẽêềếệểễìíịỉĩòóọỏõôồốộổỗơờớợởỡùúụủũưừứựửữỳýỵỷỹđ")
    return any(c in vn_chars for c in text.lower())


# ============================================================================
# Text-to-Speech (optimized)
# ============================================================================
async def tts_stream(text):
    """Convert text to MP3 speech - faster for short responses"""
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
        
        # Convert to MP3 with optimized settings
        filters = ["apad=pad_dur=0.3"]  # Less padding
        if voice == "Linh":
            filters.insert(0, "atempo=1.25")  # Slightly faster
        
        ffmpeg = await asyncio.create_subprocess_exec(
            "ffmpeg", "-i", temp_file,
            "-f", "mp3", "-ac", "1", "-ar", "44100", "-b:a", "128k",
            "-filter:a", ",".join(filters),
            "pipe:1",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        
        # Stream in larger chunks for efficiency
        while True:
            chunk = await ffmpeg.stdout.read(8192)
            if not chunk:
                break
            yield chunk
        
        await ffmpeg.wait()
        
    except asyncio.CancelledError:
        logger.warning("TTS cancelled")
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
# Music Streaming - Optimized
# ============================================================================
INITIAL_BURST_SIZE = 512 * 1024  # 512KB initial burst
CHUNK_SIZE = 8192  # Larger chunks


async def stream_music(query_or_url, websocket):
    """Stream music with optimized buffering"""
    logger.info(f"🎵 Streaming: {query_or_url[:50]}...")
    
    process = None
    try:
        yt_dlp = "/opt/homebrew/bin/yt-dlp"
        
        if query_or_url.startswith("ytsearch") or "youtube.com" in query_or_url or "youtu.be" in query_or_url:
            url = query_or_url
        elif query_or_url.startswith("http"):
            shell_cmd = f'ffmpeg -i "{query_or_url}" -t 300 -f mp3 -ac 1 -ar 44100 -b:a 128k -vn pipe:1 2>/dev/null'
            process = await asyncio.create_subprocess_shell(
                shell_cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
        else:
            url = f"ytsearch1:{query_or_url}"
        
        if not process:
            shell_cmd = f'{yt_dlp} --cookies-from-browser chrome --no-playlist -f bestaudio -o - "{url}" 2>/dev/null | ffmpeg -i pipe:0 -t 300 -f mp3 -ac 1 -ar 44100 -b:a 128k -vn pipe:1 2>/dev/null'
            process = await asyncio.create_subprocess_shell(
                shell_cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
        
        bytes_sent = 0
        try:
            while True:
                chunk = await asyncio.wait_for(process.stdout.read(CHUNK_SIZE), timeout=15)
                if not chunk:
                    break
                
                await websocket.send(chunk)
                bytes_sent += len(chunk)
                
                # Throttle after burst
                if bytes_sent > INITIAL_BURST_SIZE:
                    await asyncio.sleep(0.04)
                    
        except asyncio.TimeoutError:
            logger.warning("Stream timeout")
        
        await process.wait()
        logger.info(f"🎵 Streamed {bytes_sent/1024:.1f}KB")
            
    except Exception as e:
        logger.error(f"Stream error: {e}")
    finally:
        if process and process.returncode is None:
            try:
                process.kill()
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
async def process_audio(audio_buffer, websocket):
    """Process audio: trim → n8n → stream response"""
    import soundfile as sf
    
    try:
        audio_data = np.concatenate(audio_buffer)
        duration = len(audio_data) / SAMPLE_RATE
        logger.info(f"Processing {duration:.2f}s audio")
        
        # Skip if too short
        if duration < 0.3:
            logger.warning("Audio too short, ignoring")
            await websocket.send("AUDIO_END")
            return
        
        # Trim silence
        trimmed = trim_silence_fast(audio_data)
        
        # Save temp file
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            temp_path = f.name
            sf.write(temp_path, trimmed, SAMPLE_RATE)
        
        # Signal start
        await websocket.send("AUDIO_START")
        
        # Get response from n8n
        audio_bytes = await call_n8n(temp_path)
        os.remove(temp_path)
        
        # Stream response
        if audio_bytes:
            logger.info(f"🔊 Streaming {len(audio_bytes)} bytes")
            
            chunk_size = 8192
            for i in range(0, len(audio_bytes), chunk_size):
                chunk = audio_bytes[i:i + chunk_size]
                await websocket.send(chunk)
            
            logger.info("✅ Response streamed")
        else:
            logger.error("❌ No response from n8n")
        
        # Handle pending music
        global pending_music
        if pending_music:
            music_info = pending_music
            pending_music = None
            
            query = music_info.get("query")
            if query:
                logger.info(f"🎵 Playing queued: {query}")
                await stream_music(f"ytsearch1:{query}", websocket)
            elif music_info.get("url"):
                await stream_music(music_info["url"], websocket)
        
        # Wait for buffer drain
        await asyncio.sleep(0.5)
        await websocket.send("AUDIO_END")
        logger.info("✅ AUDIO_END sent")
        
    except asyncio.CancelledError:
        logger.info("Pipeline cancelled")
        raise
    except Exception as e:
        logger.error(f"Pipeline error: {e}")


# ============================================================================
# WebSocket Handler
# ============================================================================
active_ws = None
pending_music = None


async def handle_client(websocket):
    """Handle ESP32 client with optimized VAD"""
    global active_ws
    active_ws = websocket
    
    client_addr = websocket.remote_address
    logger.info(f"🔌 Connected: {client_addr}")
    
    audio_buffer = []
    chunk_count = 0
    recording = False
    task = None
    start_time = None
    
    try:
        async for message in websocket:
            # Text commands
            if isinstance(message, str):
                if message == "BARGE_IN":
                    logger.warning("⏹️ BARGE-IN")
                    if task and not task.done():
                        task.cancel()
                        try:
                            await task
                        except asyncio.CancelledError:
                            pass
                    audio_buffer = []
                    recording = False
                    chunk_count = 0
                    continue
                
                if message == "END":
                    logger.info(f"📨 END received ({chunk_count} chunks)")
                    
                    if audio_buffer and chunk_count >= MIN_RECORDING_CHUNKS:
                        if task and not task.done():
                            task.cancel()
                        
                        task = asyncio.create_task(
                            process_audio(list(audio_buffer), websocket)
                        )
                    else:
                        logger.warning(f"Ignoring short recording ({chunk_count} chunks)")
                        await websocket.send("AUDIO_END")
                    
                    audio_buffer = []
                    recording = False
                    chunk_count = 0
                    start_time = None
                    continue
            
            # Binary audio
            else:
                chunk = np.frombuffer(message, dtype=np.int16).astype(np.float32) / 32768.0
                audio_buffer.append(chunk)
                chunk_count += 1
                
                if not recording:
                    recording = True
                    start_time = asyncio.get_event_loop().time()
                    logger.info("🎙️ Recording...")
                
                # Timeout check
                if start_time:
                    elapsed = asyncio.get_event_loop().time() - start_time
                    if elapsed > MAX_RECORDING_SEC:
                        logger.warning("⏱️ Recording timeout")
                        await websocket.send("STOP_RECORDING")
                        recording = False
    
    except websockets.exceptions.ConnectionClosed:
        logger.info(f"Connection closed: {client_addr}")
    except Exception as e:
        logger.error(f"Handler error: {e}")
    finally:
        if task and not task.done():
            task.cancel()
        
        if active_ws == websocket:
            active_ws = None
        
        logger.info(f"🔌 Disconnected: {client_addr}")


# ============================================================================
# HTTP API (for external commands)
# ============================================================================
async def handle_music_request(request):
    """Queue music for after AI response"""
    global pending_music
    
    try:
        data = await request.json()
        query = data.get("query")
        url = data.get("url")
        
        if not active_ws:
            return web.json_response({"error": "No client"}, status=400)
        
        if not query and not url:
            return web.json_response({"error": "Missing query/url"}, status=400)
        
        logger.info(f"🎵 Queued: {query or url}")
        pending_music = {"query": query, "url": url}
        
        return web.json_response({"status": "queued"})
        
    except Exception as e:
        return web.json_response({"error": str(e)}, status=500)


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
        
        await active_ws.send("AUDIO_START")
        async for chunk in tts_stream(text):
            await active_ws.send(chunk)
        await asyncio.sleep(0.5)
        await active_ws.send("AUDIO_END")
        
        return web.json_response({"status": "ok"})
        
    except Exception as e:
        return web.json_response({"error": str(e)}, status=500)


async def handle_play_request(request):
    """Immediate audio playback"""
    try:
        data = await request.json()
        url = data.get("url")
        query = data.get("query")
        
        if not active_ws:
            return web.json_response({"error": "No client"}, status=400)
        
        if not url and not query:
            return web.json_response({"error": "Missing url/query"}, status=400)
        
        if not url and query:
            # Search YouTube
            import subprocess
            result = await asyncio.to_thread(
                subprocess.run,
                ["yt-dlp", "-f", "bestaudio", "-g", f"ytsearch1:{query}"],
                capture_output=True, text=True, timeout=10
            )
            if result.returncode == 0:
                url = result.stdout.strip()
            else:
                return web.json_response({"error": "Not found"}, status=404)
        
        logger.info(f"🎵 Playing: {url[:50]}...")
        
        await active_ws.send("AUDIO_START")
        await stream_music(url, active_ws)
        await asyncio.sleep(0.5)
        await active_ws.send("AUDIO_END")
        
        return web.json_response({"status": "ok"})
        
    except Exception as e:
        return web.json_response({"error": str(e)}, status=500)


async def handle_status(request):
    """Server status endpoint"""
    return web.json_response({
        "status": "running",
        "client_connected": active_ws is not None,
        "vad_available": VAD_AVAILABLE,
    })


async def start_http_server():
    """Start HTTP API server"""
    app = web.Application()
    app.router.add_post("/play_music", handle_music_request)
    app.router.add_post("/speak", handle_speak_request)
    app.router.add_post("/play", handle_play_request)
    app.router.add_get("/status", handle_status)
    
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "0.0.0.0", HTTP_PORT)
    await site.start()
    
    logger.info(f"🌐 HTTP API on port {HTTP_PORT}")
    logger.info(f"   GET  /status     - Server status")
    logger.info(f"   POST /speak      - TTS to ESP32")
    logger.info(f"   POST /play       - Play audio/music")
    logger.info(f"   POST /play_music - Queue music")


# ============================================================================
# Main
# ============================================================================
async def main():
    logger.info("╔════════════════════════════════════╗")
    logger.info("║   JARVIS v4 Server                ║")
    logger.info("║   Optimized for ESP32-LyraT-Mini  ║")
    logger.info("╚════════════════════════════════════╝")
    
    await start_http_server()
    
    async with websockets.serve(
        handle_client, 
        "0.0.0.0", 
        PORT, 
        max_size=64 * 1024,  # Reduced from 128KB
        ping_interval=20,
        ping_timeout=10,
    ):
        logger.info(f"🎤 WebSocket server on port {PORT}")
        logger.info("   Waiting for ESP32...")
        await asyncio.Future()


if __name__ == "__main__":
    try:
        from aiohttp import web
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Server stopped")
