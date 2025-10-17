import dayjs from 'dayjs';
import Schedule from '../models/Schedule.js';
import Device from '../models/Device.js';
import CommandLog from '../models/CommandLog.js';
import { escapeRegExp } from '../utils/regex.js';

const findDevice = async ({ deviceId, deviceName }) => {
  if (deviceId) {
    return Device.findById(deviceId);
  }
  if (deviceName) {
    return Device.findOne({ name: new RegExp(`^${escapeRegExp(deviceName)}$`, 'i') });
  }
  return null;
};

export const createSchedule = async ({
  deviceId,
  deviceName,
  action,
  payload,
  runAt,
  origin = 'api',
  naturalLanguage,
}) => {
  const device = await findDevice({ deviceId, deviceName });

  if (!device) {
    const error = new Error('Device not found');
    error.status = 404;
    throw error;
  }

  const timestamp = dayjs(runAt);
  if (!timestamp.isValid()) {
    const error = new Error('Invalid schedule timestamp');
    error.status = 400;
    throw error;
  }

  const schedule = await Schedule.create({
    device: device._id,
    deviceName: device.name,
    action,
    payload,
    runAt: timestamp.toDate(),
    origin,
    naturalLanguage,
    status: 'scheduled',
  });

  await CommandLog.create({
    device: device._id,
    deviceName: device.name,
    action,
    payload,
    origin,
    naturalLanguage,
    status: 'scheduled',
    runAt: schedule.runAt,
  });

  return schedule;
};

export const listSchedules = async () => {
  return Schedule.find().sort({ runAt: 1 });
};

export const createScheduleFromInterpretation = async (interpretation, prompt) => {
  return createSchedule({
    deviceName: interpretation.deviceName,
    action: interpretation.action,
    payload: interpretation.payload,
    runAt: interpretation.runAt,
    origin: 'natural-language',
    naturalLanguage: prompt,
  });
};
