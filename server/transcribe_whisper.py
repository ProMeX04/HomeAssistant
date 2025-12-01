import argparse
import warnings
import sys
import os

# Suppress warnings
warnings.filterwarnings("ignore")

def transcribe_audio(audio_path, model_size="medium", language=None):
    """
    Transcribe audio file using MLX Whisper (Optimized for Apple Silicon).
    """
    if not os.path.exists(audio_path):
        print(f"Error: File not found: {audio_path}", file=sys.stderr)
        return

    try:
        import mlx_whisper
    except ImportError:
        print("Error: mlx_whisper not found. Please install it with: pip install mlx-whisper", file=sys.stderr)
        return

    # Map standard model names to MLX Community Hugging Face Repos
    model_mapping = {
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

    # Get the HF repo ID or use the raw string if not in map
    model_path = model_mapping.get(model_size, model_size)

    print(f"Loading MLX Whisper model: {model_path}...", file=sys.stderr)
    print(f"Transcribing '{audio_path}'...", file=sys.stderr)

    try:
        # Options for transcription
        options = {
            "path_or_hf_repo": model_path,
            "verbose": False
        }
        if language:
            options["language"] = language
        
        # MLX Whisper transcribe
        result = mlx_whisper.transcribe(audio_path, **options)
        
        # Print the transcription to stdout
        print(result["text"].strip())
        
    except Exception as e:
        print(f"Error during transcription: {e}", file=sys.stderr)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Transcribe audio using MLX Whisper (Apple Silicon)")
    parser.add_argument("audio_file", help="Path to the audio file to transcribe")
    parser.add_argument("--model", default="medium", help="Model size (tiny, base, small, medium, large, large-v3-turbo)")
    parser.add_argument("--language", default=None, help="Language code (e.g., 'en', 'vi'). If not set, auto-detects.")

    args = parser.parse_args()

    transcribe_audio(args.audio_file, args.model, args.language)
