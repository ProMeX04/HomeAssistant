import { createSchedule, listSchedules } from '../services/scheduleService.js';

export const getSchedules = async (_req, res) => {
  const schedules = await listSchedules();
  res.json({ schedules });
};

export const createScheduleEntry = async (req, res, next) => {
  try {
    const schedule = await createSchedule({
      deviceId: req.body.deviceId,
      deviceName: req.body.deviceName,
      action: req.body.action,
      payload: req.body.payload,
      runAt: req.body.runAt,
      origin: 'api',
      naturalLanguage: req.body.naturalLanguage,
    });
    res.status(201).json({ schedule });
  } catch (error) {
    next(error);
  }
};
