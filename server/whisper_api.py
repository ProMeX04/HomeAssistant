import os
import sys
import tempfile
import warnings
from typing import Optional

from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Request
from fastapi.responses import JSONResponse
from fastapi.exceptions import RequestValidationError
import uvicorn

# Suppress warnings
warnings.filterwarnings("ignore")

# Initialize FastAPI app
app = FastAPI(title="MLX Whisper API", description="API for transcribing audio using MLX Whisper on Apple Silicon")

@app.exception_handler(RequestValidationError)
async def validation_exception_handler(request: Request, exc: RequestValidationError):
    print(f"Validation Error: {exc.errors()}", file=sys.stderr)
    return JSONResponse(
        status_code=422,
        content={"detail": exc.errors()},
    )

# Global model variable
model_path = None

# Model mapping
MODEL_MAPPING = {
    "tiny": "mlx-community/whisper-tiny-mlx",
    "base": "mlx-community/whisper-base-mlx",
    "small": "mlx-community/whisper-small-mlx",
    "medium": "mlx-community/whisper-medium-mlx",
    "large": "mlx-community/whisper-large-v3-mlx",
    "large-v2": "mlx-community/whisper-large-v2-mlx",
    "large-v3": "mlx-community/whisper-large-v3-mlx",
    "large-v3-turbo": "mlx-community/whisper-large-v3-turbo",
    "distil-large-v3": "mlx-community/distil-whisper-large-v3",
}

def get_model_path(model_size: str) -> str:
    return MODEL_MAPPING.get(model_size, model_size)

@app.on_event("startup")
async def startup_event():
    """Load the model on startup."""
    global model_path
    
    # Check if mlx_whisper is installed
    try:
        import mlx_whisper
    except ImportError:
        print("Error: mlx_whisper not found. Please install it with: pip install mlx-whisper", file=sys.stderr)
        sys.exit(1)

    # Get default model from env or use 'medium'
    default_model = os.getenv("WHISPER_MODEL", "medium")
    model_path = get_model_path(default_model)
    
    print(f"Server started. Default model set to: {model_path}")
    print("Note: MLX Whisper loads the model lazily on the first request or when explicitly loaded.")

@app.post("/transcribe")
async def transcribe(
    file: UploadFile = File(...),
    model: Optional[str] = Form(None),
    language: Optional[str] = Form(None)
):
    """
    Transcribe an uploaded audio file.
    """
    import mlx_whisper

    # Determine model to use
    current_model_path = model_path
    if model:
        current_model_path = get_model_path(model)
    
    print(f"Received transcription request. Model: {current_model_path}, Language: {language}")

    # Save uploaded file to temp file
    try:
        suffix = os.path.splitext(file.filename)[1]
        if not suffix:
            suffix = ".wav" # Default to wav if no extension
            
        with tempfile.NamedTemporaryFile(delete=False, suffix=suffix) as tmp:
            content = await file.read()
            tmp.write(content)
            tmp_path = tmp.name
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to save uploaded file: {str(e)}")

    try:
        # Options for transcription
        options = {
            "path_or_hf_repo": current_model_path,
            "verbose": False
        }
        if language:
            options["language"] = language
        
        # Transcribe
        result = mlx_whisper.transcribe(tmp_path, **options)
        text = result["text"].strip()
        
        return JSONResponse(content={"text": text, "model": current_model_path})

    except Exception as e:
        print(f"Transcription error: {e}", file=sys.stderr)
        raise HTTPException(status_code=500, detail=f"Transcription failed: {str(e)}")
    finally:
        # Cleanup temp file
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

if __name__ == "__main__":
    port = int(os.getenv("PORT", 7777))
    uvicorn.run("whisper_api:app", host="0.0.0.0", port=port, reload=False)
