#!/usr/bin/env python3
"""
Simple MLX Whisper transcriber (Apple Silicon optimized)
"""
import mlx.core as mx
import numpy as np
from huggingface_hub import hf_hub_download
import json
import struct

class MLXWhisper:
    def __init__(self, model_name="mlx-community/whisper-base.en-mlx"):
        """Initialize MLX Whisper model"""
        self.model_name = model_name
        self.model = None
        
    def load_model(self):
        """Load Whisper model"""
        # Download model files from HuggingFace
        try:
            import mlx_whisper
            print(f"Loading {self.model_name}...")
            # MLX Whisper will auto-download if needed
            self.model = mlx_whisper.load(self.model_name)
            print("Model loaded successfully!")
            return True
        except Exception as e:
            print(f"Failed to load model: {e}")
            return False
    
    def transcribe(self, audio_path_or_array):
        """Transcribe audio file or numpy array"""
        if self.model is None:
            if not self.load_model():
                return None
        
        try:
            import mlx_whisper
            result = mlx_whisper.transcribe(
                audio_path_or_array,
                path_or_hf_repo=self.model_name
            )
            return result["text"]
        except Exception as e:
            print(f"Transcription error: {e}")
            return None
