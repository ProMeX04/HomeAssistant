#!/usr/bin/env python3
"""
Async WebSocket Server for ESP32 <-> Gemini Live API
Pure asyncio implementation (no Flask/threading)
"""

import asyncio
import logging
import sys
from random import randint

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

# API Keys
GEMINI_API_KEYS = [
    "AIzaSyAl56jbXmNN83CCgIUhF3isJbyXvdBR6TE",
    "AIzaSyD-hRP3SFISp1to9aAlnxRSA8RVlIWdp6E",
    "AIzaSyDweHQxZrbRjkaoaGLhwySYLYP8UUecAvc",
    "AIzaSyAXNwDUF6w9pindu_kPvYWR7Xk8LxCs2j8",
    "AIzaSyDTcveJfEKmXSQybFFargK0CukZ4s_kzxE",
    "AIzaSyAg0ZE3zzqIs7UG3DxCKicHmTXx61Pdlbc",
]

MODEL_ID = "gemini-2.5-flash-native-audio-preview-09-2025"

SYS_INSTRUCTION = """You are a helpful AI assistant.
Your response must be short and concise.
Do not use markdown formatting.
"""


def get_api_key():
    """Get random API key"""
    key = GEMINI_API_KEYS[randint(0, len(GEMINI_API_KEYS) - 1)]
    logger.info(
        f"🔑 Using API Key (index {randint(0, len(GEMINI_API_KEYS) - 1)}/{len(GEMINI_API_KEYS)})"
    )
    return key


async def handle_client(websocket):
    """
    Handle a single ESP32 client connection
    Runs three concurrent tasks:
    1. Receive audio from ESP32
    2. Send audio to ESP32
    3. Process with Gemini (persistent session)
    """
    logger.info(f" Client connected: {websocket.remote_address}")

    # Async queues for inter-task communication
    audio_in_queue = asyncio.Queue()
    audio_out_queue = asyncio.Queue()
    stop_event = asyncio.Event()

    # Get API client
    api_key = get_api_key()
    client = genai.Client(api_key=api_key, http_options={"api_version": "v1alpha"})

    # Task 1: Receive from ESP32
    async def esp32_receiver():
        logger.info(" ESP32 Receiver Started")
        audio_buffer = bytearray()

        try:
            async for message in websocket:
                if stop_event.is_set():
                    break

                if isinstance(message, str):
                    if message == "END":
                        logger.info(
                            f" END signal -> Buffered {len(audio_buffer)} bytes"
                        )
                        if len(audio_buffer) > 0:
                            await audio_in_queue.put(bytes(audio_buffer))
                            audio_buffer.clear()
                        else:
                            logger.warning(" Received END but buffer is empty")
                else:
                    # Binary audio data
                    audio_buffer.extend(message)

        except websockets.exceptions.ConnectionClosed:
            logger.info(" ESP32 disconnected")
        except Exception as e:
            logger.error(f" ESP32 Receiver Error: {e}")
        finally:
            stop_event.set()
            logger.info(" ESP32 Receiver Stopped")

    # Task 2: Send to ESP32
    async def esp32_sender():
        logger.info(" ESP32 Sender Started")

        try:
            while not stop_event.is_set():
                try:
                    audio_chunk = await asyncio.wait_for(
                        audio_out_queue.get(), timeout=1.0
                    )
                    await websocket.send(audio_chunk)
                    logger.debug(f"Sent {len(audio_chunk)} bytes to ESP32")
                except asyncio.TimeoutError:
                    continue
                except Exception as e:
                    logger.error(f" Error sending to ESP32: {e}")
                    break
        finally:
            logger.info(" ESP32 Sender Stopped")

    # Task 3: Gemini Session (Persistent, Sequential - Like test_google_sample.py)
    async def gemini_session():
        logger.info(f" Connecting to Gemini Live ({MODEL_ID})...")

        session_config = genai.types.LiveConnectConfig(
            response_modalities=["AUDIO"],
            system_instruction=genai.types.Content(
                parts=[genai.types.Part(text=SYS_INSTRUCTION)]
            ),
        )

        try:
            async with client.aio.live.connect(
                model=MODEL_ID, config=session_config
            ) as session:
                logger.info(" Connected to Gemini Live!")
                logger.info(" Starting Sequential Turn Processing")

                while not stop_event.is_set():
                    try:
                        # 1. Wait for audio from ESP32
                        try:
                            audio_chunk = await asyncio.wait_for(
                                audio_in_queue.get(), timeout=1.0
                            )
                        except asyncio.TimeoutError:
                            continue

                        logger.info(
                            f"Turn Start: Sending {len(audio_chunk)} bytes to Gemini"
                        )

                        # 2. Send to Gemini (exactly like test_google_sample.py)
                        await session.send_realtime_input(
                            audio=genai.types.Blob(
                                data=audio_chunk, mime_type="audio/pcm;rate=16000"
                            )
                        )

                        logger.info(" Waiting for response...")

                        # 3. Receive response with timeout (blocking until turn complete or timeout)
                        turn_complete = False

                        try:

                            async def receive_with_timeout():
                                nonlocal turn_complete
                                async for response in session.receive():
                                    if stop_event.is_set():
                                        break

                                    # Handle audio output
                                    audio_sent = False
                                    if response.data:
                                        logger.info(
                                            f" Received Audio: {len(response.data)} bytes"
                                        )
                                        await audio_out_queue.put(response.data)
                                        audio_sent = True

                                    # Handle inline data (only if not already sent via response.data)
                                    if (
                                        not audio_sent
                                        and response.server_content
                                        and response.server_content.model_turn
                                    ):
                                        for (
                                            part
                                        ) in response.server_content.model_turn.parts:
                                            if part.inline_data:
                                                logger.info(
                                                    f" Received Inline: {len(part.inline_data.data)} bytes"
                                                )
                                                await audio_out_queue.put(
                                                    part.inline_data.data
                                                )

                                    # Handle text
                                    try:
                                        if response.text:
                                            logger.info(f" Gemini: {response.text}")
                                    except:
                                        pass

                                    # Check turn complete
                                    if (
                                        response.server_content
                                        and response.server_content.turn_complete
                                    ):
                                        logger.info(" Turn Complete")
                                        turn_complete = True
                                        break  # Exit receive loop

                            # Wait for response with 15-second timeout
                            await asyncio.wait_for(receive_with_timeout(), timeout=15.0)

                        except asyncio.TimeoutError:
                            logger.warning(
                                " Gemini response timeout (15s) - skipping turn"
                            )
                            turn_complete = True  # Treat as complete to continue

                        # 4. Notify ESP32
                        if turn_complete:
                            await asyncio.sleep(0.2)  # Small buffer delay
                            await websocket.send("AUDIO_END")
                            logger.info(" AUDIO_END sent to ESP32")

                    except Exception as e:
                        logger.error(f"Error processing turn: {e}")
                        await asyncio.sleep(0.5)

        except Exception as e:
            logger.error(f" Gemini Session Error: {e}")
        finally:
            stop_event.set()
            logger.info(" Gemini Session Closed")

    # Run all tasks concurrently
    try:
        await asyncio.gather(
            esp32_receiver(), esp32_sender(), gemini_session(), return_exceptions=True
        )
    except Exception as e:
        logger.error(f" Client handler error: {e}")
    finally:
        logger.info("Client session ended")


async def main():
    """Main async entry point"""
    logger.info("Starting Async WebSocket Server (Gemini Live API)...")
    logger.info("WebSocket: ws://0.0.0.0:6666/audio")

    async with websockets.serve(
        handle_client,
        "0.0.0.0",
        6666,
        ping_interval=30,  # Send ping every 30 seconds
        ping_timeout=60,  # Wait 60 seconds for pong response
    ):
        logger.info("Server is running. Press Ctrl+C to stop.")
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("\nServer stopped by user")
