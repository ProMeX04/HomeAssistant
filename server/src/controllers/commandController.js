import CommandLog from '../models/CommandLog.js';
import { dispatchDeviceCommand, handleNaturalLanguageCommand } from '../services/commandService.js';
import { transcribeAudio } from '../services/whisperService.js';

export const listCommandLogs = async (_req, res) => {
  const logs = await CommandLog.find().sort({ createdAt: -1 }).limit(100);
  res.json({ logs });
};

export const createCommand = async (req, res, next) => {
  try {
    const { deviceId, deviceName, action, payload } = req.body;
    const command = await dispatchDeviceCommand({
      deviceId,
      deviceName,
      action,
      payload,
      origin: 'api',
    });
    res.status(201).json({ command });
  } catch (error) {
    next(error);
  }
};

export const naturalLanguageCommand = async (req, res, next) => {
  try {
    const { prompt } = req.body;
    const result = await handleNaturalLanguageCommand(prompt);
    res.status(201).json(result);
  } catch (error) {
    next(error);
  }
};

export const transcribeAudioCommand = async (req, res, next) => {
  try {
    if (!req.file) {
      return res.status(400).json({ message: 'No audio file provided' });
    }

    const { path: filePath } = req.file;
    const language = req.body.language || 'vi';

    const transcription = await transcribeAudio(filePath, language);

    res.status(200).json({
      transcription,
      message: 'Audio transcribed successfully'
    });
  } catch (error) {
    next(error);
  }
};
