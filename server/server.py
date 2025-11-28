import asyncio
import logging
import os
import sys
from datetime import datetime

import edge_tts
import google.generativeai as genai
import whisper
from flask import Flask, jsonify, request, send_file
from pydub import AudioSegment

app = Flask(__name__)

# Configure logging to work with Gunicorn
logging.basicConfig(
    level=logging.INFO, format="%(message)s", stream=sys.stdout, force=True
)
logger = logging.getLogger(__name__)

# Ensure recordings directory exists
RECORDINGS_DIR = "recordings"
os.makedirs(RECORDINGS_DIR, exist_ok=True)

# Configure Gemini API
GEMINI_API_KEY = os.environ.get("GEMINI_API_KEY")
if not GEMINI_API_KEY:
    logger.warning("⚠️  WARNING: GEMINI_API_KEY not set!")
    logger.info("   Get your API key from: https://aistudio.google.com/app/apikey")
    logger.info("   Then set it: export GEMINI_API_KEY='your-api-key'")
    logger.info("   Or use: ./setup_api_key.sh")
else:
    genai.configure(api_key=GEMINI_API_KEY)
    logger.info("✅ Gemini API Configured!")

# Initialize Whisper Model for Transcription
logger.info("⏳ Loading Whisper Model (base)...")
whisper_model = whisper.load_model("base")
logger.info("✅ Whisper Model Loaded!")

# Initialize Gemini for AI Response
gemini_model = genai.GenerativeModel("gemini-2.5-flash-lite")
logger.info("✅ Gemini 2.5 Flash Model Ready!")


async def generate_tts(text, output_filename):
    """Generate TTS audio using Edge TTS"""
    communicate = edge_tts.Communicate(text, "vi-VN-NamMinhNeural")
    await communicate.save(output_filename)


def convert_audio_for_esp32(input_path, output_path):
    """Convert audio to 16kHz, 16-bit, mono WAV for ESP32"""
    audio = AudioSegment.from_file(input_path)
    audio = audio.set_frame_rate(16000).set_channels(1).set_sample_width(2)
    audio.export(output_path, format="wav")


@app.route("/upload_audio", methods=["POST"])
def upload_audio():
    """Receive WAV audio stream from ESP32, save to disk, and transcribe"""
    try:
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"recording_{timestamp}.wav"
        filepath = os.path.join(RECORDINGS_DIR, filename)

        file_size = 0
        try:
            with open(filepath, "wb") as f:
                # Read from stream in chunks
                while True:
                    chunk = request.stream.read(4096)
                    if not chunk:
                        break
                    f.write(chunk)
                    file_size += len(chunk)
        except Exception as e:
            # Catch ClientDisconnected or other read errors
            # If we got enough data, we can still try to process it
            logger.info(f"⚠️ Stream interrupted: {e}")
            if file_size < 44:
                return jsonify({"error": "Stream interrupted and file too small"}), 400

        logger.info(f"✅ Received audio: {filename} ({file_size} bytes)")

        if file_size > 44:
            try:
                with open(filepath, "r+b") as f:
                    # Fix RIFF Chunk Size (File Size - 8)
                    f.seek(4)
                    f.write((file_size - 8).to_bytes(4, byteorder="little"))

                    # Fix Data Chunk Size (File Size - 44)
                    f.seek(40)
                    f.write((file_size - 44).to_bytes(4, byteorder="little"))
                logger.info("🔧 WAV Header patched with actual size.")
            except Exception as e:
                logger.info(f"⚠️ Failed to patch WAV header: {e}")

        # Step 1: Transcribe with Whisper
        if file_size > 44:  # Check > 44 to ensure we have at least a header
            logger.info("=" * 60)
            logger.info("📝 Step 1: Transcribing audio with Whisper...")

            try:
                # Whisper transcription
                result = whisper_model.transcribe(filepath)
                transcription = result["text"].strip()

                logger.info(f"🗣️  Transcription: {transcription}")
                logger.info("-" * 60)

                # Step 2: Send transcription to Gemini for AI response
                logger.info("🤖 Step 2: Getting AI response from Gemini...")

                gemini_response = None
                audio_url = None

                if GEMINI_API_KEY:
                    try:
                        # Create a conversational prompt
                        prompt = f"""Bạn là một trợ lý ảo thông minh và thân thiện.
Người dùng vừa nói: "{transcription}"

Hãy trả lời một cách tự nhiên và hữu ích. Nếu đó là câu hỏi, hãy trả lời câu hỏi. 
Nếu đó là lệnh, hãy xác nhận bạn hiểu. Nếu đó là chào hỏi, hãy chào lại.

Trả lời ngắn gọn, tự nhiên bằng tiếng Việt (hoặc ngôn ngữ người dùng sử dụng)."""

                        response = gemini_model.generate_content(prompt)
                        gemini_response = response.text.strip()

                        logger.info(f"🧠 AI Response: {gemini_response}")

                        # Step 3: Generate TTS Audio
                        logger.info("🔊 Step 3: Generating TTS audio...")
                        tts_filename = f"response_{timestamp}.mp3"
                        tts_filepath = os.path.join(RECORDINGS_DIR, tts_filename)

                        # Run async TTS generation
                        asyncio.run(generate_tts(gemini_response, tts_filepath))
                        logger.info(f"✅ TTS Audio generated: {tts_filename}")

                        # Generate URL for the audio file (MP3)
                        audio_url = f"http://{request.host}/get_audio/{tts_filename}"
                        logger.info(f"🔗 Audio URL: {audio_url}")

                    except Exception as e:
                        logger.info(
                            f"⚠️  Gemini/TTS Error (continuing without AI response): {str(e)}"
                        )
                        import traceback

                        traceback.print_exc()
                        gemini_response = None
                else:
                    logger.info("⚠️  Gemini API Key not set, skipping AI response")

                logger.info("=" * 60)

                return jsonify(
                    {
                        "status": "success",
                        "filename": filename,
                        "size": file_size,
                        "transcription": transcription,
                        "ai_response": gemini_response,
                        "audio_url": audio_url,
                        "models": {
                            "transcription": "whisper-base",
                            "ai": "gemini-2.5-flash-lite" if gemini_response else None,
                            "tts": "edge-tts" if audio_url else None,
                        },
                    }
                ), 200

            except Exception as e:
                logger.info(f"❌ Processing Error: {str(e)}")
                import traceback

                traceback.print_exc()
                return jsonify(
                    {
                        "status": "error",
                        "message": f"Processing failed: {str(e)}",
                    }
                ), 500
        else:
            return jsonify({"status": "error", "message": "Empty or invalid file"}), 400

    except Exception as e:
        import traceback

        traceback.print_exc()
        logger.info(f"❌ Error: {str(e)}")
        return jsonify({"error": str(e)}), 500


@app.route("/get_audio/<filename>")
def get_audio(filename):
    """Serve the generated audio file"""
    return send_file(os.path.join(RECORDINGS_DIR, filename))


@app.route("/health", methods=["GET"])
def health():
    """Health check endpoint"""
    return jsonify({"status": "ok"}), 200


if __name__ == "__main__":
    logger.info("🎙️  Audio Server Starting...")
    logger.info(f"📁 Recordings will be saved to: {os.path.abspath(RECORDINGS_DIR)}")
    logger.info("🌐 HTTP Server running on http://0.0.0.0:6666")

    # Run Flask
    app.run(host="0.0.0.0", port=6666, debug=False)
