import OpenAI from 'openai';
import fs from 'fs';

let openai = null;

if (process.env.OPENAI_API_KEY) {
    openai = new OpenAI({
        apiKey: process.env.OPENAI_API_KEY,
    });
}

/**
 * Transcribe audio file using OpenAI Whisper API
 * @param {string} filePath - Path to the audio file
 * @param {string} language - Language code (e.g., 'vi' for Vietnamese, 'en' for English)
 * @returns {Promise<string>} - Transcribed text
 */
export const transcribeAudio = async (filePath, language = 'vi') => {
    if (!openai) {
        throw new Error('OpenAI API key is not configured. Please set OPENAI_API_KEY in .env file');
    }

    try {
        const audioFile = fs.createReadStream(filePath);

        const transcription = await openai.audio.transcriptions.create({
            file: audioFile,
            model: 'whisper-1',
            language: language,
            response_format: 'json',
        });

        return transcription.text;
    } catch (error) {
        console.error('Whisper transcription error:', error);
        throw new Error(`Failed to transcribe audio: ${error.message}`);
    } finally {
        // Clean up the temporary file
        try {
            if (fs.existsSync(filePath)) {
                fs.unlinkSync(filePath);
            }
        } catch (cleanupError) {
            console.error('Failed to cleanup temporary file:', cleanupError);
        }
    }
};
