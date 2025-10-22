import Device from '../models/Device.js';
import SensorReading from '../models/SensorReading.js';
import { dispatchDeviceCommand } from '../services/commandService.js';
import { registerDeviceTopics } from '../services/mqttService.js';

export const listDevices = async (_req, res) => {
  const devices = await Device.find().sort({ name: 1 });
  res.json({ devices });
};

export const createDevice = async (req, res) => {
  const sensors = Array.isArray(req.body.sensors)
    ? req.body.sensors.map((sensor) => ({
        sensorId: sensor.sensorId,
        name: sensor.name,
        metric: sensor.metric,
        unit: sensor.unit,
        lastValue: sensor.lastValue,
        lastRecordedAt: sensor.lastRecordedAt,
      }))
    : undefined;

  const device = await Device.create({
    identifier: req.body.identifier,
    name: req.body.name,
    type: req.body.type,
    location: req.body.location,
    topicCommand: req.body.topicCommand,
    topicState: req.body.topicState,
    topicTelemetry: req.body.topicTelemetry,
    sensors,
  });
  await registerDeviceTopics(device);
  res.status(201).json({ device });
};

export const updateDevice = async (req, res, next) => {
  try {
    const { id } = req.params;
    const { name, location } = req.body || {};

    const setUpdate = {};
    const unsetUpdate = {};

    if (name !== undefined) {
      if (typeof name !== 'string' || name.trim().length === 0) {
        return res.status(400).json({ message: 'Device name is required' });
      }

      const trimmedName = name.trim();
      const nameInUse = await Device.exists({ _id: { $ne: id }, name: trimmedName });
      if (nameInUse) {
        return res.status(409).json({ message: 'Device name is already in use' });
      }

      setUpdate.name = trimmedName;
    }

    if (location !== undefined) {
      if (location === null) {
        unsetUpdate.location = '';
      } else if (typeof location === 'string') {
        const trimmedLocation = location.trim();
        if (trimmedLocation.length === 0) {
          unsetUpdate.location = '';
        } else {
          setUpdate.location = trimmedLocation;
        }
      } else {
        return res.status(400).json({ message: 'Location must be a string or null' });
      }
    }

    if (Object.keys(setUpdate).length === 0 && Object.keys(unsetUpdate).length === 0) {
      return res.status(400).json({ message: 'No update fields provided' });
    }

    const update = {};
    if (Object.keys(setUpdate).length > 0) {
      update.$set = setUpdate;
    }
    if (Object.keys(unsetUpdate).length > 0) {
      update.$unset = unsetUpdate;
    }

    const device = await Device.findByIdAndUpdate(id, update, {
      new: true,
      runValidators: true,
    });

    if (!device) {
      return res.status(404).json({ message: 'Device not found' });
    }

    res.json({ device });
  } catch (error) {
    next(error);
  }
};

export const sendDeviceCommand = async (req, res, next) => {
  try {
    const { action, payload } = req.body;
    const { id } = req.params;
    const command = await dispatchDeviceCommand({
      deviceId: id,
      action,
      payload,
      origin: 'api',
    });
    res.status(201).json({ command });
  } catch (error) {
    next(error);
  }
};

export const getDeviceReadings = async (req, res, next) => {
  try {
    const { id } = req.params;
    const readings = await SensorReading.find({ device: id })
      .sort({ recordedAt: -1 })
      .limit(50);
    res.json({ readings });
  } catch (error) {
    next(error);
  }
};
