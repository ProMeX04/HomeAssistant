import Device from '../models/Device.js';
import CommandLog from '../models/CommandLog.js';
import { publishCommand } from './mqttService.js';
import { interpretCommand } from './geminiService.js';
import { createScheduleFromInterpretation } from './scheduleService.js';
import { escapeRegExp } from '../utils/regex.js';

const findDeviceByName = async (name) => {
  if (!name) return null;
  return Device.findOne({ name: new RegExp(`^${escapeRegExp(name)}$`, 'i') });
};

export const dispatchDeviceCommand = async ({
  deviceId,
  deviceName,
  action,
  payload,
  origin = 'api',
  naturalLanguage,
}) => {
  const device = deviceId
    ? await Device.findById(deviceId)
    : await findDeviceByName(deviceName);

  if (!device) {
    const error = new Error('Device not found');
    error.status = 404;
    throw error;
  }

  const commandLog = await CommandLog.create({
    device: device._id,
    deviceName: device.name,
    action,
    payload,
    origin,
    naturalLanguage,
    status: 'pending',
  });

  const messagePayload = {
    action,
    ...((payload && typeof payload === 'object') ? payload : {}),
  };

  if (payload && typeof payload !== 'object') {
    messagePayload.value = payload;
  }

  await publishCommand(device, messagePayload);

  commandLog.status = 'sent';
  await commandLog.save();

  return commandLog;
};

export const handleNaturalLanguageCommand = async (prompt) => {
  const interpretation = await interpretCommand(prompt);

  if (interpretation.type === 'schedule') {
    const schedule = await createScheduleFromInterpretation(interpretation, prompt);
    return {
      type: 'schedule',
      schedule,
    };
  }

  const command = await dispatchDeviceCommand({
    deviceName: interpretation.deviceName,
    action: interpretation.action,
    payload: interpretation.payload,
    origin: 'natural-language',
    naturalLanguage: prompt,
  });

  return {
    type: 'command',
    command,
  };
};
