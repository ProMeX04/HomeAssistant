#!/usr/bin/env python3
"""
Streaming WebSocket Server with n8n Integration
Pipeline: Audio Chunks -> Whisper Streaming -> n8n Webhook -> TTS -> Audio
"""

import asyncio
import json
import logging
import os
import re  # Added for URL detection
import subprocess
import sys
import tempfile

import aiohttp
import numpy as np
import torch
import websockets

# Logging Setup
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(message)s",
    stream=sys.stdout,
    force=True,
)
logger = logging.getLogger(__name__)

# Configuration
PORT = 6666
WHISPER_MODEL_SIZE = "base.en"
N8N_WEBHOOK_URL = "http://localhost:5678/webhook-test/753a58bd-1b12-4643-858f-9249c3477da5"

# Load Silero VAD Model
logger.info("Loading Silero VAD model...")
vad_model, vad_utils = torch.hub.load(
    repo_or_dir="snakers4/silero-vad",
    model="silero_vad",
    force_reload=False,
    onnx=False,
)
(get_speech_timestamps, _, _, _, _) = vad_utils
logger.info("Silero VAD loaded successfully")


def trim_silence(audio_data, sample_rate=16000, threshold=0.5):
    """
    Trim silence from beginning and end of audio using Silero VAD

    Args:
        audio_data: numpy array of float32 audio samples (-1.0 to 1.0)
        sample_rate: sample rate of the audio
        threshold: VAD confidence threshold (0.0 to 1.0)

    Returns:
        Trimmed audio as numpy array
    """
    try:
        # Convert to torch tensor
        audio_tensor = torch.from_numpy(audio_data).float()

        # Get speech timestamps
        speech_timestamps = get_speech_timestamps(
            audio_tensor,
            vad_model,
            sampling_rate=sample_rate,
            threshold=threshold,
            min_speech_duration_ms=100,
            min_silence_duration_ms=100,
        )

        if not speech_timestamps:
            logger.warning("No speech detected in audio")
            return audio_data

        # Get first and last speech segments
        start_sample = speech_timestamps[0]["start"]
        end_sample = speech_timestamps[-1]["end"]

        # Trim audio
        trimmed_audio = audio_data[start_sample:end_sample]

        original_duration = len(audio_data) / sample_rate
        trimmed_duration = len(trimmed_audio) / sample_rate
        trimmed_amount = original_duration - trimmed_duration

        logger.info(
            f"Trimmed {trimmed_amount:.2f}s silence "
            f"(Original: {original_duration:.2f}s → Trimmed: {trimmed_duration:.2f}s)"
        )

        return trimmed_audio

    except Exception as e:
        logger.error(f"VAD trim error: {e}")
        return audio_data  # Return original if trimming fails


def is_vietnamese(text):
    """Detect if text contains Vietnamese characters"""
    vietnamese_chars = set(
        "àáạảãâầấậẩẫăằắặẳẵèéẹẻẽêềếệểễìíịỉĩòóọỏõôồốộổỗơờớợởỡùúụủũưừứựửữỳýỵỷỹđ"
    )
    return any(char in vietnamese_chars for char in text.lower())


async def text_to_speech_stream(text):
    """Convert text to speech using macOS 'say' and yield PCM 48kHz chunks"""
    if not text:
        return

    # Select voice based on language
    voice = "Linh" if is_vietnamese(text) else "Samantha"
    logger.info(f"TTS Voice: {voice}")

    temp_aiff_path = None
    process = None
    process_ffmpeg = None

    try:
        with tempfile.NamedTemporaryFile(suffix=".aiff", delete=False) as temp_aiff:
            temp_aiff_path = temp_aiff.name

        # Use macOS 'say' command
        process = await asyncio.create_subprocess_exec(
            "say",
            "-v",
            voice,
            "-o",
            temp_aiff_path,
            text,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL,
        )
        await process.wait()

        # Convert to raw PCM 48kHz mono
        # If voice is Linh, speed up by 20% (atempo=1.2)
        ffmpeg_args = [
            "ffmpeg",
            "-i",
            temp_aiff_path,
            "-f",
            "s16le",
            "-ac",
            "1",
            "-ar",
            "48000",
        ]

        if voice == "Linh":
            ffmpeg_args.extend(["-filter:a", "atempo=1.2"])

        ffmpeg_args.append("pipe:1")

        process_ffmpeg = await asyncio.create_subprocess_exec(
            *ffmpeg_args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )

        chunk_size = 4096
        while True:
            data = await process_ffmpeg.stdout.read(chunk_size)
            if not data:
                break
            yield data

        await process_ffmpeg.wait()

    except asyncio.CancelledError:
        logger.warning("TTS streaming cancelled")
        raise  # Propagate cancellation
    except Exception as e:
        logger.error(f"TTS Error: {e}")
    finally:
        # Cleanup processes
        if process and process.returncode is None:
            try:
                process.kill()
            except:
                pass
        if process_ffmpeg and process_ffmpeg.returncode is None:
            try:
                process_ffmpeg.kill()
            except:
                pass
        
        # Cleanup temp file
        if temp_aiff_path and os.path.exists(temp_aiff_path):
            try:
                os.remove(temp_aiff_path)
            except:
                pass


async def stream_audio_url(url, websocket):
    """Stream audio from a URL to websocket using ffmpeg"""
    logger.info(f"Streaming music from URL: {url}")
    process = None
    try:
        # Use yt-dlp to get the direct audio URL if it's a YouTube link
        if "youtube.com" in url or "youtu.be" in url:
            try:
                cmd = ["yt-dlp", "-f", "bestaudio", "-g", url]
                result = await asyncio.to_thread(subprocess.run, cmd, capture_output=True, text=True, timeout=10)
                if result.returncode == 0 and result.stdout.strip():
                    url = result.stdout.strip()
                    logger.info(f"Resolved YouTube URL: {url[:50]}...")
            except Exception as e:
                logger.warning(f"Failed to resolve YouTube URL with yt-dlp: {e}")

        process = await asyncio.create_subprocess_exec(
            "ffmpeg",
            "-reconnect",
            "1",
            "-reconnect_streamed",
            "1",
            "-reconnect_delay_max",
            "5",
            "-i",
            url,
            "-f",
            "s16le",
            "-ac",
            "1",
            "-ar",
            "48000",
            "-vn",
            "pipe:1",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )

        chunk_size = 4096
        while True:
            data = await process.stdout.read(chunk_size)
            if not data:
                break
            await websocket.send(data)

        await process.wait()
        logger.info("Music streaming finished.")
    
    except asyncio.CancelledError:
        logger.warning("Music streaming cancelled")
        raise
    except Exception as e:
        logger.error(f"Error streaming music: {e}")
    finally:
        if process and process.returncode is None:
            try:
                process.kill()
                logger.info("Killed ffmpeg process")
            except:
                pass

async def call_n8n_webhook_audio(audio_path):
    """Call n8n webhook with AUDIO FILE"""
    async with aiohttp.ClientSession() as session:
        try:
            logger.info(f"Sending AUDIO to n8n: {audio_path}")

            data = aiohttp.FormData()
            data.add_field(
                "file",
                open(audio_path, "rb"),
                filename="audio.wav",
                content_type="audio/wav",
            )

            # Optional: Add text field if needed
            # data.add_field('type', 'audio')

            async with session.post(N8N_WEBHOOK_URL, data=data) as response:
                if response.status == 200:
                    body = await response.text()

                    # Case 1: n8n Streaming Response (NDJSON)
                    if '{"type":"item"' in body or '{"type":"begin"' in body:
                        full_text = ""
                        logger.info("Parsing n8n streaming response...")
                        for line in body.splitlines():
                            if not line.strip():
                                continue
                            try:
                                data = json.loads(line)
                                if data.get("type") == "item" and "content" in data:
                                    full_text += data["content"]
                                elif isinstance(data, dict) and "output" in data:
                                    full_text += data["output"]
                            except:
                                pass
                        return full_text

                    # Case 2: Standard JSON Response
                    try:
                        data = json.loads(body)
                        if isinstance(data, dict):
                            # Return the full dictionary to handle structured commands
                            return data
                        elif isinstance(data, list) and len(data) > 0:
                            # If it's a list, return the first item
                            return data[0]
                        return {"text": str(data)}
                    except:
                        # Case 3: Raw Text
                        return {"text": body}
                else:
                    logger.error(f"n8n returned status: {response.status}")
                    return {"text": "I'm sorry, I couldn't connect to the brain."}
        except Exception as e:
            logger.error(f"Error calling n8n: {e}")
            return {"text": "I'm sorry, there was an error processing your request."}


async def process_n8n_turn_audio(audio_path, websocket):
    """Process AUDIO with n8n and stream audio response"""

    await websocket.send("AUDIO_START")

    # Call n8n with AUDIO
    n8n_response = await call_n8n_webhook_audio(audio_path)
    
    # Extract text and potential commands
    response_text = n8n_response.get("output", n8n_response.get("text", ""))
    music_query = n8n_response.get("music", n8n_response.get("song", None))
    
    logger.info(f"n8n Response: {n8n_response}")

    music_url = None

    # 1. Check for explicit Music Command from n8n JSON
    if music_query:
        logger.info(f"n8n requested music via JSON command: '{music_query}'")
        try:
            cmd = ["yt-dlp", "-f", "bestaudio", "-g", f"ytsearch1:{music_query}"]
            result = await asyncio.to_thread(subprocess.run, cmd, capture_output=True, text=True, timeout=15)
            if result.returncode == 0 and result.stdout.strip():
                music_url = result.stdout.strip()
                logger.info(f"Found song URL via yt-dlp")
            else:
                logger.warning("Song not found via yt-dlp")
        except Exception as e:
            logger.error(f"yt-dlp search error: {e}")

    # 2. Detect [MUSIC: song name] tag (Legacy support)
    if not music_url:
        music_tag_match = re.search(r"\[MUSIC:\s*(.*?)\]", response_text, re.IGNORECASE)
        if music_tag_match:
            song_query = music_tag_match.group(1).strip()
            logger.info(f"n8n requested music search via TAG: '{song_query}'")

            # Remove tag from TTS
            response_text = response_text.replace(music_tag_match.group(0), "")

            # Search locally using yt-dlp
            try:
                cmd = ["yt-dlp", "-f", "bestaudio", "-g", f"ytsearch1:{song_query}"]
                result = await asyncio.to_thread(subprocess.run, cmd, capture_output=True, text=True, timeout=15)
                if result.returncode == 0 and result.stdout.strip():
                    music_url = result.stdout.strip()
                    logger.info(f"Found song URL via yt-dlp")
                else:
                    logger.warning("Song not found via yt-dlp")
            except Exception as e:
                logger.error(f"yt-dlp search error: {e}")

    # 3. Detect direct URL (Fallback)
    if not music_url:
        url_pattern = r"http[s]?://(?:[a-zA-Z]|[0-9]|[$-_@.&+]|[!*\\(\\),]|(?:%[0-9a-fA-F][0-9a-fA-F]))+"
        urls = re.findall(url_pattern, response_text)
        if urls:
            for url in urls:
                if "youtube.com" in url or "youtu.be" in url:
                    music_url = url
                    response_text = response_text.replace(url, "playing music")
                    logger.info(f"Detected direct Music URL: {music_url}")
                    break

    # Stream TTS
    if response_text.strip():
        async for audio_chunk in text_to_speech_stream(response_text):
            await websocket.send(audio_chunk)

    # Stream Music
    if music_url:
        await stream_audio_url(music_url, websocket)

    await asyncio.sleep(0.2)
    await websocket.send("AUDIO_END")
    logger.info("AUDIO_END sent")
async def handle_client(websocket):
    """Handle WebSocket Client with Real-time VAD"""
    global active_websocket
    active_websocket = websocket
    logger.info(f"Client connected: {websocket.remote_address}")

    # Buffer to store ALL audio for this turn
    full_audio_buffer = []

    # Real-time VAD state
    recording_active = False
    silence_chunks = 0
    SILENCE_THRESHOLD = 8
    MIN_RECORDING_CHUNKS = 10
    total_chunks = 0
    
    # Track the active processing task
    processing_task = None

    try:
        async for message in websocket:
            if isinstance(message, str):
                if message == "BARGE_IN":
                    logger.warning("BARGE-IN DETECTED! Cancelling active tasks...")
                    if processing_task and not processing_task.done():
                        processing_task.cancel()
                        try:
                            await processing_task
                        except asyncio.CancelledError:
                            logger.info("Processing task successfully cancelled")
                    
                    # Clear any buffered audio
                    full_audio_buffer = []
                    recording_active = False
                    continue

                if message == "END":
                    logger.info("Received END signal from ESP32")

                    # Save accumulated audio to WAV
                    if full_audio_buffer:
                        # ... (Audio saving logic remains same, extracted for brevity if needed, but keeping inline for now)
                        # To keep this clean, I'll assume the logic is inside the task
                        
                        # Cancel previous task if somehow still running
                        if processing_task and not processing_task.done():
                            processing_task.cancel()
                        
                        # Start new processing task
                        # We need to pass a copy of the buffer because we clear it immediately
                        buffer_copy = list(full_audio_buffer)
                        processing_task = asyncio.create_task(
                            run_pipeline_task(buffer_copy, websocket)
                        )
                    else:
                        logger.warning("No audio received")
                        await websocket.send("AUDIO_END")

                    # Reset for next turn
                    full_audio_buffer = []
                    recording_active = False
                    total_chunks = 0
                    silence_chunks = 0

            else:
                # Binary audio data - Real-time VAD processing
                audio_chunk = (
                    np.frombuffer(message, dtype=np.int16).astype(np.float32) / 32768.0
                )
                full_audio_buffer.append(audio_chunk)
                total_chunks += 1
                
                # ... (VAD logic remains same) ...
                if not recording_active:
                    recording_active = True
                    logger.info("Recording started - Real-time VAD active")

                try:
                    audio_tensor = torch.from_numpy(audio_chunk).float()
                    speech_timestamps = get_speech_timestamps(
                        audio_tensor, vad_model, sampling_rate=16000, threshold=0.5,
                        min_speech_duration_ms=100, min_silence_duration_ms=100
                    )

                    if speech_timestamps:
                        silence_chunks = 0
                    else:
                        silence_chunks += 1
                        if total_chunks >= MIN_RECORDING_CHUNKS and silence_chunks >= SILENCE_THRESHOLD:
                            logger.info("Silence detected - Sending STOP_RECORDING")
                            await websocket.send("STOP_RECORDING")
                            recording_active = False
                            silence_chunks = 0
                except Exception:
                    pass

    except Exception as e:
        logger.error(f"Error: {e}")
    finally:
        if processing_task and not processing_task.done():
            processing_task.cancel()
        
        if active_websocket == websocket:
            active_websocket = None
            
        logger.info("Connection closed")


async def run_pipeline_task(audio_buffer, websocket):
    """Wrapper to run the full pipeline in a cancellable task"""
    try:
        import soundfile as sf
        from datetime import datetime
        
        audio_data = np.concatenate(audio_buffer)
        
        # ... (Save and Trim logic) ...
        # Simplified for brevity in this replacement block, but preserving functionality
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        debug_dir = "debug_audio"
        os.makedirs(debug_dir, exist_ok=True)
        
        # Trim silence
        trimmed_audio = trim_silence(audio_data, sample_rate=16000, threshold=0.5)
        
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as temp_wav:
            temp_wav_path = temp_wav.name
            sf.write(temp_wav_path, trimmed_audio, 16000)

        logger.info(f"Processing audio pipeline for {len(trimmed_audio)} samples")
        
        await process_n8n_turn_audio(temp_wav_path, websocket)
        
        os.remove(temp_wav_path)
        
    except asyncio.CancelledError:
        logger.info("Pipeline task cancelled")
        raise
    except Exception as e:
        logger.error(f"Pipeline error: {e}")



# Global variable to track the active client
active_websocket = None
HTTP_PORT = 6667

async def handle_http_music(request):
    """Handle HTTP request to play music"""
    try:
        data = await request.json()
        query = data.get("query")
        url = data.get("url")
        
        if not active_websocket:
            return web.json_response({"error": "No active client connected"}, status=400)
            
        if not query and not url:
            return web.json_response({"error": "Missing 'query' or 'url'"}, status=400)
            
        logger.info(f"Received HTTP music request: query='{query}', url='{url}'")
        
        # Resolve URL if query provided
        music_url = url
        if query and not music_url:
            try:
                cmd = ["yt-dlp", "-f", "bestaudio", "-g", f"ytsearch1:{query}"]
                result = await asyncio.to_thread(subprocess.run, cmd, capture_output=True, text=True, timeout=15)
                if result.returncode == 0 and result.stdout.strip():
                    music_url = result.stdout.strip()
                    logger.info(f"Found song URL via yt-dlp: {music_url}")
                else:
                    return web.json_response({"error": "Song not found"}, status=404)
            except Exception as e:
                return web.json_response({"error": f"Search failed: {str(e)}"}, status=500)

        # Start streaming in background
        if music_url:
            asyncio.create_task(stream_audio_url(music_url, active_websocket))
            return web.json_response({"status": "playing", "url": music_url})
            
        return web.json_response({"error": "Could not resolve URL"}, status=400)

    except Exception as e:
        logger.error(f"HTTP Handler Error: {e}")
        return web.json_response({"error": str(e)}, status=500)

async def start_http_server():
    """Start the sidecar HTTP server for n8n commands"""
    app = web.Application()
    app.router.add_post('/play_music', handle_http_music)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, '0.0.0.0', HTTP_PORT)
    await site.start()
    logger.info(f"HTTP Command Server listening on port {HTTP_PORT}")

async def main():
    logger.info(f"Starting n8n Streaming Server on port {PORT}...")
    
    # Start HTTP Server
    await start_http_server()
    
    # Start WebSocket Server
    async with websockets.serve(handle_client, "0.0.0.0", PORT, max_size=128 * 1024):
        await asyncio.Future()


if __name__ == "__main__":
    try:
        from aiohttp import web  # Import here to ensure it's available
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
