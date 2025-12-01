#!/usr/bin/env python3
"""
Async WebSocket Server for ESP32 - Full Audio Buffering Mode
Pipeline: Receive FULL Audio → Whisper (STT) → Gemini (LLM) → TTS → Audio
Difference from streaming: Buffer ALL audio before transcription (better accuracy)
"""

import argparse
import asyncio
import io
import logging
import os
import subprocess
import sys
import tempfile
from datetime import datetime
from random import randint

import numpy as np
import websockets
from google import genai

# Logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(message)s",
    stream=sys.stdout,
    force=True,
)
logger = logging.getLogger(__name__)

# Debug Configuration
DEBUG_SAVE_AUDIO = True  # Set to False to disable audio saving
DEBUG_AUDIO_DIR = "debug_audio"

# Create debug directory
if DEBUG_SAVE_AUDIO:
    os.makedirs(DEBUG_AUDIO_DIR, exist_ok=True)
    logger.info(f"🔍 Debug mode enabled - Audio will be saved to: {DEBUG_AUDIO_DIR}/")

# API Keys
GEMINI_API_KEYS = [
    "AIzaSyAl56jbXmNN83CCgIUhF3isJbyXvdBR6TE",
    "AIzaSyD-hRP3SFISp1to9aAlnxRSA8RVlIWdp6E",
    "AIzaSyDweHQxZrbRjkaoaGLhwySYLYP8UUecAvc",
    "AIzaSyAXNwDUF6w9pindu_kPvYWR7Xk8LxCs2j8",
    "AIzaSyDTcveJfEKmXSQybFFargK0CukZ4s_kzxE",
    "AIzaSyAg0ZE3zzqIs7UG3DxCKicHmTXx61Pdlbc",
]

# Configuration
PORT = 6666
WHISPER_MODEL_SIZE = "base.en"
LLM_MODEL_ID = "gemini-2.5-flash-lite"
SYS_INSTRUCTION = """You are a helpful AI assistant named Jarvis.
You are conversing in English.
Keep your responses concise, natural, and conversational.
Do not use markdown formatting.
IMPORTANT: If the user's input starts with 'Jarvis' or 'Ding', ignore that part and process the rest of the command.
"""

# Global Whisper model
whisper_model = None


def load_whisper():
    global whisper_model
    try:
        logger.info(f"Loading MLX Whisper model ({WHISPER_MODEL_SIZE})...")
        mlx_model_name = f"mlx-community/whisper-{WHISPER_MODEL_SIZE}-mlx"
        logger.info(f"Using MLX model: {mlx_model_name}")

        # Store model name for transcription
        whisper_model = {"type": "mlx", "model_name": mlx_model_name}
        logger.info("MLX Whisper loaded successfully (Apple Silicon optimized).")
        return True
    except Exception as e:
        logger.warning(f"Failed to load MLX Whisper: {e}")
        logger.warning("Falling back to Gemini for STT.")
        return False


# Initialize Whisper
USE_WHISPER = load_whisper()


def get_api_key():
    return GEMINI_API_KEYS[randint(0, len(GEMINI_API_KEYS) - 1)]


# --- Tool Definitions ---
from datetime import datetime

import requests


def get_current_time():
    """Get current date and time"""
    now = datetime.now()
    return {
        "time": now.strftime("%H:%M:%S"),
        "date": now.strftime("%Y-%m-%d"),
        "day": now.strftime("%A"),
        "formatted": now.strftime("%A, %B %d, %Y at %I:%M %p"),
    }


def get_weather(city: str = "Hanoi"):
    """Get current weather for a city"""
    try:
        url = f"https://wttr.in/{city}?format=j1"
        response = requests.get(url, timeout=5)
        if response.status_code == 200:
            data = response.json()
            current = data["current_condition"][0]
            return {
                "city": city,
                "temperature_c": current["temp_C"],
                "temperature_f": current["temp_F"],
                "description": current["weatherDesc"][0]["value"],
                "humidity": current["humidity"],
                "wind_kph": current["windspeedKmph"],
            }
    except:
        pass
    return {"error": "Could not fetch weather data"}


def web_search(query: str):
    """Search the web for information"""
    return {
        "query": query,
        "message": "Web search functionality - integrate with your preferred search API",
    }


def play_music(song_name: str):
    """Search and play a song from YouTube"""
    try:
        import json
        import subprocess

        search_query = f"ytsearch1:{song_name}"
        cmd = [
            "yt-dlp",
            "-j",
            "--skip-download",
            "-f",
            "bestaudio",
            search_query,
        ]

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)

        if result.returncode == 0 and result.stdout:
            video_info = json.loads(result.stdout.strip())
            return {
                "status": "found",
                "title": video_info.get("title", "Unknown"),
                "artist": video_info.get("uploader", "Unknown"),
                "duration": video_info.get("duration", 0),
                "url": video_info.get("url", ""),
                "webpage_url": video_info.get("webpage_url", ""),
            }
        else:
            return {
                "status": "not_found",
                "message": f"Could not find song: {song_name}",
            }

    except subprocess.TimeoutExpired:
        return {"status": "error", "message": "Search timeout"}
    except Exception as e:
        return {"status": "error", "message": str(e)}


# Define tools for Gemini
tools = [
    {
        "function_declarations": [
            {
                "name": "get_current_time",
                "description": "Get the current date and time",
                "parameters": {"type": "object", "properties": {}},
            },
            {
                "name": "get_weather",
                "description": "Get current weather information for a city",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "city": {
                            "type": "string",
                            "description": "The city name to get weather for",
                        }
                    },
                },
            },
            {
                "name": "web_search",
                "description": "Search the web for information",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "query": {"type": "string", "description": "The search query"}
                    },
                    "required": ["query"],
                },
            },
            {
                "name": "play_music",
                "description": "Search and play a song or music from YouTube",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "song_name": {
                            "type": "string",
                            "description": "The name of the song or artist to search for",
                        }
                    },
                    "required": ["song_name"],
                },
            },
        ]
    }
]

# Tool execution mapping
tool_functions = {
    "get_current_time": get_current_time,
    "get_weather": get_weather,
    "web_search": web_search,
    "play_music": play_music,
}


def execute_tool(function_name: str, args: dict):
    """Execute a tool function"""
    if function_name in tool_functions:
        try:
            return tool_functions[function_name](**args)
        except Exception as e:
            return {"error": str(e)}
    return {"error": f"Unknown function: {function_name}"}


async def text_to_speech_stream(text):
    """
    Convert text to speech using macOS 'say' command and yield PCM 48kHz chunks
    """
    if not text:
        return

    try:
        # Create temp file for AIFF output from say
        with tempfile.NamedTemporaryFile(suffix=".aiff", delete=False) as temp_aiff:
            temp_aiff_path = temp_aiff.name

        # Generate audio with macOS 'say' command
        process = await asyncio.create_subprocess_exec(
            "say",
            "-v",
            "Samantha",
            "-o",
            temp_aiff_path,
            text,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL,
        )
        await process.wait()

        # Convert AIFF to PCM 48kHz Mono using FFmpeg
        process_ffmpeg = await asyncio.create_subprocess_exec(
            "ffmpeg",
            "-i",
            temp_aiff_path,
            "-f",
            "s16le",
            "-ac",
            "1",
            "-ar",
            "48000",
            "pipe:1",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )

        pcm_data, _ = await process_ffmpeg.communicate()

        # Cleanup temp file
        if os.path.exists(temp_aiff_path):
            os.remove(temp_aiff_path)

        # Yield chunks
        chunk_size = 4096
        for i in range(0, len(pcm_data), chunk_size):
            yield pcm_data[i : i + chunk_size]

    except Exception as e:
        logger.error(f"MacOS Say TTS Error: {e}")
        if "temp_aiff_path" in locals() and os.path.exists(temp_aiff_path):
            os.remove(temp_aiff_path)


async def stream_audio_url(url, websocket):
    """Stream audio from a URL to the websocket using ffmpeg"""
    logger.info(f"Streaming music from URL: {url}")
    try:
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
    except Exception as e:
        logger.error(f"Error streaming music: {e}")


async def process_llm_turn(text, websocket):
    """
    Send text to LLM and stream audio response with real-time TTS
    """
    logger.info(f"User: {text}")

    # Create new client with random API key
    api_key = get_api_key()
    gemini_client = genai.Client(api_key=api_key)

    logger.info("Generating response (streaming)...")

    # Signal start of new audio response
    await websocket.send("AUDIO_START")

    music_to_play = None

    try:
        # Create streaming request with tools
        response_stream = gemini_client.models.generate_content_stream(
            model=LLM_MODEL_ID,
            contents=text,
            config=genai.types.GenerateContentConfig(
                system_instruction=SYS_INSTRUCTION,
                max_output_tokens=1024,
                tools=tools,
                thinkingConfig={
                    "thinkingBudget": -1,
                },
            ),
        )

        # Accumulate text and split into sentences for streaming TTS
        sentence_buffer = ""
        sentence_terminators = {".", "!", "?", "。", "！", "？"}

        full_response = ""

        for chunk in response_stream:
            if chunk.candidates and chunk.candidates[0].content.parts:
                part = chunk.candidates[0].content.parts[0]

                if hasattr(part, "function_call") and part.function_call:
                    # Handle tool call
                    function_call = part.function_call
                    function_name = function_call.name
                    function_args = dict(function_call.args)

                    logger.info(f"Tool call: {function_name}({function_args})")
                    result = execute_tool(function_name, function_args)
                    logger.info(f"Tool result: {result}")

                    # Check if this is music playback
                    if (
                        function_name == "play_music"
                        and result.get("status") == "found"
                    ):
                        music_to_play = result.get("url")

                    # Re-generate with tool result
                    response_stream = gemini_client.models.generate_content_stream(
                        model=LLM_MODEL_ID,
                        contents=[
                            genai.types.Content(
                                role="user", parts=[genai.types.Part(text=text)]
                            ),
                            genai.types.Content(
                                role="model",
                                parts=[genai.types.Part(function_call=function_call)],
                            ),
                            genai.types.Content(
                                role="function",
                                parts=[
                                    genai.types.Part(
                                        function_response=genai.types.FunctionResponse(
                                            name=function_name, response=result
                                        )
                                    )
                                ],
                            ),
                        ],
                        config=genai.types.GenerateContentConfig(
                            system_instruction=SYS_INSTRUCTION,
                            max_output_tokens=1024,
                        ),
                    )
                    # Process the new stream with tool results
                    for tool_chunk in response_stream:
                        if (
                            tool_chunk.candidates
                            and tool_chunk.candidates[0].content.parts
                        ):
                            tool_part = tool_chunk.candidates[0].content.parts[0]
                            if hasattr(tool_part, "text") and tool_part.text:
                                chunk_text = tool_part.text
                                sentence_buffer += chunk_text
                                full_response += chunk_text

                                # Check if we have a complete sentence
                                for char in sentence_buffer:
                                    if char in sentence_terminators:
                                        sentence = sentence_buffer[
                                            : sentence_buffer.index(char) + 1
                                        ].strip()
                                        if sentence:
                                            logger.info(
                                                f"Streaming sentence: {sentence}"
                                            )
                                            async for (
                                                audio_chunk
                                            ) in text_to_speech_stream(sentence):
                                                await websocket.send(audio_chunk)

                                        sentence_buffer = sentence_buffer[
                                            sentence_buffer.index(char) + 1 :
                                        ].strip()
                                        break
                    break

                # Get text from chunk
                if hasattr(part, "text") and part.text:
                    chunk_text = part.text
                    sentence_buffer += chunk_text
                    full_response += chunk_text

                    # Check if we have a complete sentence
                    for char in sentence_buffer:
                        if char in sentence_terminators:
                            sentence = sentence_buffer[
                                : sentence_buffer.index(char) + 1
                            ].strip()
                            if sentence:
                                logger.info(f"Streaming sentence: {sentence}")
                                async for audio_chunk in text_to_speech_stream(
                                    sentence
                                ):
                                    await websocket.send(audio_chunk)

                            sentence_buffer = sentence_buffer[
                                sentence_buffer.index(char) + 1 :
                            ].strip()
                            break

        # Process any remaining text in buffer
        if sentence_buffer.strip():
            logger.info(f"Streaming final: {sentence_buffer.strip()}")
            async for audio_chunk in text_to_speech_stream(sentence_buffer.strip()):
                await websocket.send(audio_chunk)

        logger.info(f"Jarvis: {full_response.strip()}")

    except Exception as e:
        logger.error(f"LLM Streaming Error: {e}")
        import traceback

        traceback.print_exc()
        # Send error message
        error_text = "I'm sorry, I encountered an error."
        async for audio_chunk in text_to_speech_stream(error_text):
            await websocket.send(audio_chunk)

    # End Turn
    await asyncio.sleep(0.2)
    await websocket.send("AUDIO_END")
    logger.info("AUDIO_END sent.")

    return music_to_play


def save_audio_debug(audio_buffer, prefix="audio"):
    """
    Save audio buffer to WAV file for debugging
    """
    if not DEBUG_SAVE_AUDIO or len(audio_buffer) == 0:
        return None

    try:
        import numpy as np
        import soundfile as sf

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{prefix}_{timestamp}.wav"
        filepath = os.path.join(DEBUG_AUDIO_DIR, filename)

        # Convert PCM to float32 array
        audio_array = (
            np.frombuffer(audio_buffer, dtype=np.int16).astype(np.float32) / 32768.0
        )

        # Save to WAV file (16kHz mono)
        sf.write(filepath, audio_array, 16000)

        logger.info(f"💾 Saved debug audio: {filepath} ({len(audio_buffer)} bytes)")
        return filepath
    except Exception as e:
        logger.error(f"Failed to save debug audio: {e}")
        return None


async def transcribe_audio(audio_buffer):
    """
    Transcribe audio buffer using Whisper or Gemini
    Returns: transcribed text string
    """
    text = ""

    # Try MLX Whisper first
    if USE_WHISPER and whisper_model and len(audio_buffer) > 0:
        logger.info(f"Transcribing {len(audio_buffer)} bytes with MLX Whisper...")
        try:
            import tempfile

            import mlx_whisper
            import numpy as np
            import soundfile as sf

            # Convert PCM to float32 array
            audio_array = (
                np.frombuffer(audio_buffer, dtype=np.int16).astype(np.float32) / 32768.0
            )

            # Save to temporary WAV file
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
                sf.write(tmp.name, audio_array, 16000)
                tmp_path = tmp.name

            # Transcribe with MLX Whisper
            result = mlx_whisper.transcribe(
                tmp_path,
                path_or_hf_repo=whisper_model["model_name"],
                verbose=False,
            )
            text = result["text"].strip()

            # Cleanup temp file
            import os

            os.remove(tmp_path)

            logger.info(f"✅ MLX Transcription: {text}")

        except Exception as e:
            logger.error(f"MLX Whisper Error: {e}")
            import traceback

            traceback.print_exc()

    # Fallback to Gemini STT if needed
    if not text and len(audio_buffer) > 0:
        logger.info("Using Gemini for STT (Fallback)...")
        try:
            api_key = get_api_key()
            client = genai.Client(api_key=api_key)

            # Convert PCM to WAV
            process = subprocess.Popen(
                [
                    "ffmpeg",
                    "-f",
                    "s16le",
                    "-ar",
                    "16000",
                    "-ac",
                    "1",
                    "-i",
                    "pipe:0",
                    "-f",
                    "wav",
                    "pipe:1",
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            wav_data, _ = process.communicate(input=audio_buffer)

            response = client.models.generate_content(
                model="gemini-2.0-flash-lite-preview-02-05",
                contents=[
                    genai.types.Part(
                        inline_data=genai.types.Blob(
                            data=wav_data, mime_type="audio/wav"
                        )
                    ),
                    genai.types.Part(
                        text="Transcribe this audio to English text exactly. Do not add any other text."
                    ),
                ],
            )
            text = response.text.strip()
            logger.info(f"✅ Gemini Transcription: {text}")
        except Exception as e:
            logger.error(f"Gemini STT Error: {e}")

    return text


async def handle_client(websocket):
    """
    Handle ESP32 Client - Full Audio Buffering Mode
    Receives ALL audio before transcription
    """
    logger.info(f"Client connected: {websocket.remote_address}")

    audio_buffer = bytearray()
    music_task = None

    try:
        async for message in websocket:
            # Cancel music task if receiving ANY data
            if music_task and not music_task.done():
                logger.info("Interrupting music playback...")
                music_task.cancel()
                try:
                    await music_task
                except asyncio.CancelledError:
                    pass
                music_task = None

            if isinstance(message, str):
                if message == "END":
                    logger.info(
                        f"🎯 Received END signal - Total audio: {len(audio_buffer)} bytes"
                    )

                    # Save audio for debugging
                    save_audio_debug(audio_buffer, prefix="received")

                    # Now transcribe the FULL audio buffer
                    text = await transcribe_audio(audio_buffer)

                    # Clear buffer for next turn
                    audio_buffer.clear()

                    if text:
                        # Process LLM and send response
                        music_url = await process_llm_turn(text, websocket)
                        if music_url:
                            # Start streaming music in background
                            music_task = asyncio.create_task(
                                stream_audio_url(music_url, websocket)
                            )
                    else:
                        logger.warning("⚠️ No transcription result")
                        await websocket.send("AUDIO_END")

            else:
                # Binary audio data - JUST BUFFER IT (no processing yet)
                audio_buffer.extend(message)
                logger.debug(
                    f"📦 Buffered audio chunk: {len(message)} bytes (Total: {len(audio_buffer)} bytes)"
                )

    except websockets.exceptions.ConnectionClosed:
        logger.info("Client disconnected")
        if music_task and not music_task.done():
            music_task.cancel()
    except Exception as e:
        logger.error(f"Connection Error: {e}")
        import traceback

        traceback.print_exc()
    finally:
        logger.info("Connection closed.")


async def main():
    logger.info(f"🚀 Starting FULL AUDIO Server on port {PORT}...")
    if USE_WHISPER:
        logger.info("✅ Using MLX Whisper (Apple Silicon optimized).")
    else:
        logger.info("⚠️ Using Gemini STT Fallback (Whisper not available).")

    async with websockets.serve(
        handle_client,
        "0.0.0.0",
        PORT,
        ping_interval=None,
        ping_timeout=None,
        max_size=128 * 1024,  # 128KB max message size
        max_queue=32,
    ):
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Server stopped.")
