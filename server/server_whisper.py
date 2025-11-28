#!/usr/bin/env python3
"""
Async WebSocket Server for ESP32 using Whisper Streaming + Gemini + EdgeTTS/Say
Pipeline: Audio Stream -> Whisper Streaming (STT) -> Gemini (LLM) -> TTS -> Audio
Specialized for English.
"""

import asyncio
import logging
import sys
import os
import io
import tempfile
import subprocess
import argparse
import numpy as np
from random import randint

import websockets
from google import genai
# import edge_tts  # Not used - using macOS 'say' instead

# Import whisper_online for streaming
try:
    import whisper_online
    HAS_WHISPER_ONLINE = True
except ImportError:
    HAS_WHISPER_ONLINE = False

# Logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(message)s",
    stream=sys.stdout,
    force=True,
)
logger = logging.getLogger(__name__)

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
"""
TTS_VOICE = "en-US-AriaNeural"

# Global ASR instance (loaded once)
asr_instance = None
online_asr_proc = None

def load_whisper_streaming():
    global asr_instance
    if not HAS_WHISPER_ONLINE:
        logger.warning("whisper_online module not found.")
    
    try:
        # Use MLX Whisper (optimized for Apple Silicon)
        import mlx_whisper
        logger.info(f"Loading MLX Whisper model ({WHISPER_MODEL_SIZE})...")
        # Model will be auto-downloaded from mlx-community on HuggingFace
        # Convert model name to MLX format
        mlx_model_name = f"mlx-community/whisper-{WHISPER_MODEL_SIZE}-mlx"
        logger.info(f"Using MLX model: {mlx_model_name}")
        
        # Store model name for transcription
        asr_instance = {
            'type': 'mlx',
            'model_name': mlx_model_name
        }
        logger.info("MLX Whisper loaded successfully (Apple Silicon optimized).")
        return True
    except Exception as e:
        logger.warning(f"Failed to load MLX Whisper: {e}")
        logger.warning("Falling back to Gemini for STT.")
        return False

# Initialize Whisper
USE_WHISPER_STREAMING = load_whisper_streaming()


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
        "formatted": now.strftime("%A, %B %d, %Y at %I:%M %p")
    }

def get_weather(city: str = "Hanoi"):
    """Get current weather for a city"""
    try:
        # Using wttr.in - free weather API
        url = f"https://wttr.in/{city}?format=j1"
        response = requests.get(url, timeout=5)
        if response.status_code == 200:
            data = response.json()
            current = data['current_condition'][0]
            return {
                "city": city,
                "temperature_c": current['temp_C'],
                "temperature_f": current['temp_F'],
                "description": current['weatherDesc'][0]['value'],
                "humidity": current['humidity'],
                "wind_kph": current['windspeedKmph']
            }
    except:
        pass
    return {"error": "Could not fetch weather data"}

def web_search(query: str):
    """Search the web for information"""
    # This is a placeholder - you can integrate with DuckDuckGo API or similar
    return {
        "query": query,
        "message": "Web search functionality - integrate with your preferred search API"
    }

# Define tools for Gemini
tools = [
    {
        "function_declarations": [
            {
                "name": "get_current_time",
                "description": "Get the current date and time",
                "parameters": {
                    "type": "object",
                    "properties": {}
                }
            },
            {
                "name": "get_weather",
                "description": "Get current weather information for a city",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "city": {
                            "type": "string",
                            "description": "The city name to get weather for"
                        }
                    }
                }
            },
            {
                "name": "web_search",
                "description": "Search the web for information",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "query": {
                            "type": "string",
                            "description": "The search query"
                        }
                    },
                    "required": ["query"]
                }
            }
        ]
    }
]

# Tool execution mapping
tool_functions = {
    "get_current_time": get_current_time,
    "get_weather": get_weather,
    "web_search": web_search
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
    Convert text to speech using macOS 'say' command and yield PCM 16kHz chunks
    """
    if not text:
        return

    try:
        # Create temp file for AIFF output from say
        with tempfile.NamedTemporaryFile(suffix=".aiff", delete=False) as temp_aiff:
            temp_aiff_path = temp_aiff.name
        
        # Generate audio file using macOS 'say'
        process = await asyncio.create_subprocess_exec(
            "say", "-o", temp_aiff_path, text,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL
        )
        await process.wait()
        
        # Convert AIFF to PCM 16kHz Mono using FFmpeg
        process_ffmpeg = await asyncio.create_subprocess_exec(
            "ffmpeg",
            "-i", temp_aiff_path,
            "-f", "s16le",       # Output format: signed 16-bit little-endian PCM
            "-ac", "1",          # Channels: 1 (Mono)
            "-ar", "16000",      # Sample rate: 16000 Hz
            "pipe:1",            # Output to stdout
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL
        )
        
        pcm_data, _ = await process_ffmpeg.communicate()
        
        # Cleanup temp file
        if os.path.exists(temp_aiff_path):
            os.remove(temp_aiff_path)
            
        # Yield chunks
        chunk_size = 4096
        for i in range(0, len(pcm_data), chunk_size):
            yield pcm_data[i:i+chunk_size]

    except Exception as e:
        logger.error(f"MacOS Say TTS Error: {e}")
        if 'temp_aiff_path' in locals() and os.path.exists(temp_aiff_path):
            os.remove(temp_aiff_path)


async def process_llm_turn(text, websocket, gemini_client):
    """
    Send text to LLM and stream audio response with real-time TTS
    Uses streaming generation + sentence-by-sentence synthesis
    """
    logger.info(f"User: {text}")
    
    # LLM (Gemini) with streaming and tool support
    logger.info("Generating response (streaming)...")
    
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
                }
            )
        )
        
        # Accumulate text and split into sentences for streaming TTS
        sentence_buffer = ""
        sentence_terminators = {'.', '!', '?', '。', '！', '？'}
        
        full_response = ""  # Store full response for logging
        
        # Note: generate_content_stream returns a sync generator, not async
        for chunk in response_stream:
            # Check for function calls
            if chunk.candidates and chunk.candidates[0].content.parts:
                part = chunk.candidates[0].content.parts[0]
                
                if hasattr(part, 'function_call') and part.function_call:
                    # Handle tool call
                    function_call = part.function_call
                    function_name = function_call.name
                    function_args = dict(function_call.args)
                    
                    logger.info(f"Tool call: {function_name}({function_args})")
                    result = execute_tool(function_name, function_args)
                    logger.info(f"Tool result: {result}")
                    
                    # Re-generate with tool result
                    response_stream = gemini_client.models.generate_content_stream(
                        model=LLM_MODEL_ID,
                        contents=[
                            genai.types.Content(
                                role="user",
                                parts=[genai.types.Part(text=text)]
                            ),
                            genai.types.Content(
                                role="model",
                                parts=[genai.types.Part(function_call=function_call)]
                            ),
                            genai.types.Content(
                                role="function",
                                parts=[genai.types.Part(
                                    function_response=genai.types.FunctionResponse(
                                        name=function_name,
                                        response=result
                                    )
                                )]
                            )
                        ],
                        config=genai.types.GenerateContentConfig(
                            system_instruction=SYS_INSTRUCTION,
                            max_output_tokens=1024,
                            tools=tools
                        )
                    )
                    continue
                
                # Get text from chunk
                if hasattr(part, 'text') and part.text:
                    chunk_text = part.text
                    sentence_buffer += chunk_text
                    full_response += chunk_text
                    
                    # Check if we have a complete sentence
                    for char in sentence_buffer:
                        if char in sentence_terminators:
                            # Found sentence end - synthesize and stream
                            sentence = sentence_buffer[:sentence_buffer.index(char) + 1].strip()
                            if sentence:
                                logger.info(f"Streaming sentence: {sentence}")
                                # Stream audio for this sentence (await is OK here)
                                async for audio_chunk in text_to_speech_stream(sentence):
                                    await websocket.send(audio_chunk)
                            
                            # Remove processed sentence from buffer
                            sentence_buffer = sentence_buffer[sentence_buffer.index(char) + 1:].strip()
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





async def handle_client(websocket):
    """
    Handle ESP32 Client
    """
    logger.info(f"Client connected: {websocket.remote_address}")
    
    # Initialize Gemini Client
    api_key = get_api_key()
    client = genai.Client(api_key=api_key)

    audio_buffer = bytearray()

    try:
        async for message in websocket:
            if isinstance(message, str):
                if message == "END":
                    logger.info("Received END.")
                    
                    text = ""
                    
                    # Transcribe with MLX Whisper or fallback to Gemini
                    if USE_WHISPER_STREAMING and asr_instance and len(audio_buffer) > 0:
                        logger.info(f"Transcribing {len(audio_buffer)} bytes with MLX Whisper...")
                        try:
                            import mlx_whisper
                            import tempfile
                            import numpy as np
                            import soundfile as sf
                            
                            # Convert PCM to float32 array
                            audio_array = np.frombuffer(audio_buffer, dtype=np.int16).astype(np.float32) / 32768.0
                            
                            # Save to temporary WAV file
                            with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tmp:
                                sf.write(tmp.name, audio_array, 16000)
                                tmp_path = tmp.name
                            
                            # Transcribe with MLX Whisper
                            result = mlx_whisper.transcribe(
                                tmp_path,
                                path_or_hf_repo=asr_instance['model_name'],
                                verbose=False
                            )
                            text = result["text"].strip()
                            
                            # Cleanup temp file
                            import os
                            os.remove(tmp_path)
                            
                            logger.info(f"MLX Transcription: {text}")
                            
                        except Exception as e:
                            logger.error(f"MLX Whisper Error: {e}")
                            # Fall through to Gemini
                    
                    # Fallback to Gemini STT if needed
                    if not text and len(audio_buffer) > 0:
                        logger.info("Using Gemini for STT (Fallback)...")
                        try:
                            # Convert PCM to WAV
                            process = subprocess.Popen(
                                ["ffmpeg", "-f", "s16le", "-ar", "16000", "-ac", "1", "-i", "pipe:0", "-f", "wav", "pipe:1"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
                            )
                            wav_data, _ = process.communicate(input=audio_buffer)
                            
                            response = client.models.generate_content(
                                model="gemini-2.0-flash-lite-preview-02-05",
                                contents=[
                                    genai.types.Part(
                                        inline_data=genai.types.Blob(
                                            data=wav_data, 
                                            mime_type="audio/wav"
                                        )
                                    ),
                                    genai.types.Part(text="Transcribe this audio to English text exactly. Do not add any other text.")
                                ]
                            )
                            text = response.text.strip()
                        except Exception as e:
                            logger.error(f"Gemini STT Error: {e}")
                        
                    audio_buffer.clear()

                    if text:
                        await process_llm_turn(text, websocket, client)
                    else:
                        await websocket.send("AUDIO_END")

            else:
                # Binary audio data - buffer it
                audio_buffer.extend(message)
                
    except websockets.exceptions.ConnectionClosed:
        logger.info("Client disconnected")
    except Exception as e:
        logger.error(f"Connection Error: {e}")
    finally:
        logger.info("Connection closed.")

async def main():
    logger.info(f"Starting Whisper Server on port {PORT}...")
    if USE_WHISPER_STREAMING:
        logger.info("Using MLX Whisper (Apple Silicon optimized).")
    else:
        logger.info("Using Gemini STT Fallback (Whisper not available).")
    
    async with websockets.serve(handle_client, "0.0.0.0", PORT):
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    try: 
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Server stopped.")
