import asyncio
import logging
import queue
import sys
import threading
from random import randint  # Initialize Flask

from flask import Flask
from flask_sock import Sock
from google import genai

app = Flask(__name__)
sock = Sock(app)

# Logging Setup
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(message)s",
    stream=sys.stdout,
    force=True,
)
logger = logging.getLogger(__name__)

GEMINI_API_KEYS = [
    "AIzaSyAl56jbXmNN83CCgIUhF3isJbyXvdBR6TE",
    "AIzaSyD-hRP3SFISp1to9aAlnxRSA8RVlIWdp6E",
    "AIzaSyDweHQxZrbRjkaoaGLhwySYLYP8UUecAvc",
    "AIzaSyAXNwDUF6w9pindu_kPvYWR7Xk8LxCs2j8",
    "AIzaSyDTcveJfEKmXSQybFFargK0CukZ4s_kzxE",
    "AIzaSyAg0ZE3zzqIs7UG3DxCKicHmTXx61Pdlbc",
]

MODEL_ID = "gemini-2.5-flash-native-audio-preview-09-2025"
if not GEMINI_API_KEYS or GEMINI_API_KEYS[0] == "AIzaSy...":
    logger.warning("GEMINI_API_KEYS are not set! Please edit server_ws.py")

# Round-robin key rotation
current_key_index = 0
key_lock = threading.Lock()


def get_next_api_key():
    global current_key_index
    with key_lock:
        key = GEMINI_API_KEYS[randint(0, len(GEMINI_API_KEYS) - 1)]
        current_key_index = (current_key_index + 1) % len(GEMINI_API_KEYS)
        logger.info(
            f"🔑 Using API Key index: {current_key_index} (Total: {len(GEMINI_API_KEYS)})"
        )
        return key


# client = genai.Client(api_key=GEMINI_API_KEY, http_options={"api_version": "v1alpha"}) # Moved to session

# System Instruction
SYS_INSTRUCTION = """Bạn là trợ lý ảo thông minh (Jarvis). 
Người dùng giao tiếp bằng giọng nói. 
Hãy trả lời ngắn gọn, tự nhiên, đi thẳng vào vấn đề bằng tiếng Việt."""

# Global Session Handle for Resumption
previous_session_handle = None
session_lock = threading.Lock()


@sock.route("/audio")
def audio_stream(ws):
    """
    Bridge ESP32 Audio (WebSocket) <-> Gemini Live API (BidiStreaming)
    """
    logger.info("🔌 Client connected (ESP32)")

    # Get API Key for this session
    api_key = get_next_api_key()
    client = genai.Client(api_key=api_key, http_options={"api_version": "v1alpha"})

    # Queues for cross-thread communication
    audio_in_queue = queue.Queue()  # ESP32 -> Gemini
    audio_out_queue = queue.Queue()  # Gemini -> ESP32
    stop_event = threading.Event()

    # ------------------------------------------------------------------
    # 1. ESP32 Input Handler (Thread)
    # Reads audio from WebSocket and puts into audio_in_queue
    # ------------------------------------------------------------------
    # ------------------------------------------------------------------
    # 1. ESP32 Input Handler (Thread)
    # Reads audio from WebSocket, BUFFERS it, and sends to Gemini ONCE at END
    # ------------------------------------------------------------------
    def esp32_receiver():
        logger.info("🎧 ESP32 Receiver Thread Started")
        audio_buffer = bytearray()  # Buffer to store full turn audio

        try:
            while not stop_event.is_set():
                data = ws.receive()
                # logger.debug(f"Received from ESP32: {len(data) if isinstance(data, bytes) else data}")

                if data is None:
                    logger.info("🔌 ESP32 disconnected")
                    break

                if isinstance(data, str):
                    if data == "END":
                        # ESP32 finished speaking -> Send buffered audio to Gemini
                        logger.info(
                            f"📥 ESP32 sent END marker -> Sending {len(audio_buffer)} bytes to Gemini"
                        )

                        if len(audio_buffer) > 0:
                            # Put the WHOLE buffer into the queue as one item
                            audio_in_queue.put(bytes(audio_buffer))
                            audio_buffer.clear()  # Clear for next turn
                        else:
                            logger.warning("⚠️ Received END but audio buffer is empty")
                            # Still trigger response even if empty (maybe just noise?)
                            # audio_in_queue.put(b"")

                        continue
                else:
                    # Binary audio data -> Append to buffer
                    audio_buffer.extend(data)

        except Exception as e:
            logger.error(f"❌ ESP32 Receiver Error: {e}")
        finally:
            stop_event.set()
            logger.info("🛑 ESP32 Receiver Thread Stopped")

    # ------------------------------------------------------------------
    # 2. ESP32 Output Handler (Thread)
    # Reads audio from audio_out_queue and sends to WebSocket
    # ------------------------------------------------------------------
    def esp32_sender():
        logger.info("🔊 ESP32 Sender Thread Started")
        try:
            while not stop_event.is_set():
                try:
                    # Wait for audio with timeout to check stop_event
                    chunk = audio_out_queue.get(timeout=0.1)
                    if chunk:
                        logger.info(f"📤 Sending {len(chunk)} bytes to ESP32")
                        ws.send(chunk)
                except queue.Empty:
                    continue
                except Exception as e:
                    logger.error(f"❌ ESP32 Sender Error: {e}")
                    break
        finally:
            stop_event.set()
            logger.info("🛑 ESP32 Sender Thread Stopped")

    # ------------------------------------------------------------------
    # Gemini Live Session (Persistent, Sequential - Like test_google_sample.py)
    # ------------------------------------------------------------------
    async def gemini_live_session():
        logger.info(f"🚀 Connecting to Gemini Live ({MODEL_ID})...")

        session_config = genai.types.LiveConnectConfig(
            response_modalities=["AUDIO"],
            system_instruction=genai.types.Content(
                parts=[genai.types.Part(text=SYS_INSTRUCTION)]
            ),
        )

        async with client.aio.live.connect(
            model=MODEL_ID, config=session_config
        ) as session:
            logger.info("✅ Connected to Gemini Live!")
            logger.info("🔄 Starting Sequential Turn Processing Loop")

            while not stop_event.is_set():
                try:
                    # 1. Wait for audio from ESP32 (blocking with timeout)
                    try:
                        full_audio_chunk = await asyncio.to_thread(
                            audio_in_queue.get, timeout=1.0
                        )
                    except queue.Empty:
                        continue

                    logger.info(
                        f"🚀 Turn Start: Sending {len(full_audio_chunk)} bytes to Gemini"
                    )

                    # 2. Send audio (exactly like test_google_sample.py)
                    await session.send_realtime_input(
                        audio=genai.types.Blob(
                            data=full_audio_chunk, mime_type="audio/pcm;rate=16000"
                        )
                    )

                    logger.info("⏳ Waiting for response...")

                    # 3. Receive response (blocking until turn_complete)
                    turn_complete = False
                    async for response in session.receive():
                        # Handle Audio Output
                        if response.data:
                            logger.info(
                                f"🔊 Received Audio: {len(response.data)} bytes"
                            )
                            audio_out_queue.put(response.data)

                        # Handle Inline Data
                        if (
                            response.server_content
                            and response.server_content.model_turn
                        ):
                            for part in response.server_content.model_turn.parts:
                                if part.inline_data:
                                    logger.info(
                                        f"Received Inline Data: {len(part.inline_data.data)} bytes"
                                    )
                                    audio_out_queue.put(part.inline_data.data)

                        # Handle Text
                        try:
                            if response.text:
                                logger.info(f"🤖 Gemini: {response.text}")
                        except:
                            pass

                        # Check for turn completion
                        if (
                            response.server_content
                            and response.server_content.turn_complete
                        ):
                            logger.info("Turn Complete")
                            turn_complete = True
                            break  # Exit receive loop, ready for next turn

                    # 4. Notify ESP32 that response is complete
                    if turn_complete:
                        import time

                        time.sleep(0.2)  # Small delay for audio playback buffer
                        ws.send("AUDIO_END")
                        logger.info("📤 AUDIO_END sent to ESP32")

                except Exception as e:
                    logger.error(f"❌ Error processing turn: {e}")
                    # Try to recover for next turn
                    await asyncio.sleep(0.5)

    # ------------------------------------------------------------------
    # Main Execution Flow
    # ------------------------------------------------------------------

    # Wrapper to run the async function in a new event loop in a thread
    def run_gemini_session_in_thread():
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(gemini_live_session())
        except Exception as e:
            logger.error(f"❌ Error in Gemini Live Session thread: {e}")
        finally:
            loop.close()
            stop_event.set()  # Ensure all threads stop if this one fails

    # Start ESP32 Threads
    t_recv = threading.Thread(target=esp32_receiver)
    t_send = threading.Thread(target=esp32_sender)
    t_gemini = threading.Thread(target=run_gemini_session_in_thread)

    t_recv.start()
    t_send.start()
    t_gemini.start()

    # Wait for stop
    while not stop_event.is_set():
        import time

        time.sleep(1)

    # Cleanup
    t_recv.join()
    t_send.join()
    t_gemini.join()
    logger.info("👋 Session Ended")


@app.route("/health")
def health():
    return {"status": "ok"}, 200


if __name__ == "__main__":
    logger.info("🚀 WebSocket Audio Server Starting (Gemini Live API)...")
    logger.info("WebSocket: ws://0.0.0.0:6666/audio")
    app.run(host="0.0.0.0", port=6666, debug=False)
