# Server Console Output Examples

Đây là các ví dụ output khi server nhận và xử lý audio từ ESP32.

## Example 1: Câu hỏi đơn giản

```
✅ Received audio: recording_20251126_003000.wav (160044 bytes)
🔧 WAV Header patched with actual size.
============================================================
📝 Step 1: Transcribing audio with Whisper...
🗣️  Transcription: Xin chào, bạn tên là gì?
------------------------------------------------------------
🤖 Step 2: Getting AI response from Gemini...
💬 AI Response: Xin chào! Tôi là trợ lý ảo của bạn. Tôi chưa có tên riêng, nhưng bạn có thể gọi tôi là "Trợ lý" nhé! Tôi có thể giúp gì cho bạn hôm nay?
============================================================
```

## Example 2: Hỏi thông tin

```
✅ Received audio: recording_20251126_003100.wav (192088 bytes)
🔧 WAV Header patched with actual size.
============================================================
📝 Step 1: Transcribing audio with Whisper...
🗣️  Transcription: Hôm nay là ngày mấy?
------------------------------------------------------------
🤖 Step 2: Getting AI response from Gemini...
💬 AI Response: Tôi không có khả năng truy cập thông tin thời gian thực. Để biết ngày hôm nay, bạn có thể kiểm tra trên điện thoại, máy tính, hoặc đồng hồ của mình nhé!
============================================================
```

## Example 3: Lệnh điều khiển

```
✅ Received audio: recording_20251126_003200.wav (144032 bytes)
🔧 WAV Header patched with actual size.
============================================================
📝 Step 1: Transcribing audio with Whisper...
🗣️  Transcription: Bật đèn phòng khách
------------------------------------------------------------
🤖 Step 2: Getting AI response from Gemini...
💬 AI Response: Tôi hiểu bạn muốn bật đèn phòng khách. Tuy nhiên, hiện tại tôi chưa được kết nối với hệ thống điều khiển thiết bị. Để thực hiện lệnh này, bạn cần tích hợp tôi với hệ thống smart home của mình.
============================================================
```

## Example 4: Tiếng Anh

```
✅ Received audio: recording_20251126_003300.wav (176056 bytes)
🔧 WAV Header patched with actual size.
============================================================
📝 Step 1: Transcribing audio with Whisper...
🗣️  Transcription: What's the weather like today?
------------------------------------------------------------
🤖 Step 2: Getting AI response from Gemini...
💬 AI Response: I don't have access to real-time information, including weather data. To check the weather, you can use a weather app or search online!
============================================================
```

## Example 5: Không có Gemini API Key

```
✅ Received audio: recording_20251126_003400.wav (152040 bytes)
🔧 WAV Header patched with actual size.
============================================================
📝 Step 1: Transcribing audio with Whisper...
🗣️  Transcription: Bạn có thể giúp tôi không?
------------------------------------------------------------
🤖 Step 2: Getting AI response from Gemini...
⚠️  Gemini API Key not set, skipping AI response
============================================================
```

## Example 6: Gemini Error (nhưng vẫn có transcription)

```
✅ Received audio: recording_20251126_003500.wav (168048 bytes)
🔧 WAV Header patched with actual size.
============================================================
📝 Step 1: Transcribing audio with Whisper...
🗣️  Transcription: Test test 1 2 3
------------------------------------------------------------
🤖 Step 2: Getting AI response from Gemini...
⚠️  Gemini Error (continuing without AI response): 429 Resource has been exhausted
============================================================
```

## Console Output khi Start Server

```bash
$ venv/bin/python server.py
⏳ Loading Whisper Model (base)...
✅ Whisper Model Loaded!
⚠️  WARNING: GEMINI_API_KEY not set!
   Get your API key from: https://aistudio.google.com/app/apikey
   Then set it: export GEMINI_API_KEY='your-api-key'
✅ Gemini 2.5 Flash Model Ready!
🎙️  Audio Server Starting...
📁 Recordings will be saved to: /path/to/recordings
🌐 HTTP Server running on http://0.0.0.0:6666
 * Serving Flask app 'server'
 * Debug mode: off
WARNING: This is a development server. Do not use it in a production deployment.
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:6666
 * Running on http://192.168.1.100:6666
```

## Console Output khi có Gemini API Key

```bash
$ export GEMINI_API_KEY='AIza...'
$ venv/bin/python server.py
⏳ Loading Whisper Model (base)...
✅ Whisper Model Loaded!
✅ Gemini API Configured!
✅ Gemini 2.5 Flash Model Ready!
🎙️  Audio Server Starting...
📁 Recordings will be saved to: /path/to/recordings
🌐 HTTP Server running on http://0.0.0.0:6666
 * Serving Flask app 'server'
 * Debug mode: off
WARNING: This is a development server. Do not use it in a production deployment.
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:6666
 * Running on http://192.168.1.100:6666
```

## JSON Response Examples

### Success với AI Response
```json
{
  "status": "success",
  "filename": "recording_20251126_003000.wav",
  "size": 160044,
  "transcription": "Xin chào, bạn khỏe không?",
  "ai_response": "Xin chào! Cảm ơn bạn đã hỏi. Tôi là một trợ lý ảo nên tôi luôn hoạt động tốt. Còn bạn thì sao? Tôi có thể giúp gì cho bạn hôm nay không?",
  "models": {
    "transcription": "whisper-base",
    "ai": "gemini-2.5-flash-lite"
  }
}
```

### Success không có AI Response (API key not set)
```json
{
  "status": "success",
  "filename": "recording_20251126_003000.wav",
  "size": 160044,
  "transcription": "Xin chào, bạn khỏe không?",
  "ai_response": null,
  "models": {
    "transcription": "whisper-base",
    "ai": null
  }
}
```

### Error
```json
{
  "status": "error",
  "message": "Processing failed: [Errno 2] No such file or directory..."
}
```
