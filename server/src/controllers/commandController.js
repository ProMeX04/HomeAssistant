import CommandLog from '../models/CommandLog.js';
import { dispatchDeviceCommand, handleNaturalLanguageCommand } from '../services/commandService.js';

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
