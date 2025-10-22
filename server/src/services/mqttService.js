import mqtt from 'mqtt';
import Device from '../models/Device.js';
import SensorReading from '../models/SensorReading.js';

let mqttClient = null;

const toTrimmedString = (value) => {
  if (typeof value !== 'string') {
    return null;
  }
  const trimmed = value.trim();
  return trimmed.length > 0 ? trimmed : null;
};

const pickString = (source, ...keys) => {
  if (!source || typeof source !== 'object') {
    return null;
  }
  for (const key of keys) {
    const candidate = toTrimmedString(source[key]);
    if (candidate) {
      return candidate;
    }
  }
  return null;
};

const extractDeviceIdentifier = (payload) => {
  const direct = pickString(payload, 'deviceId', 'device_id', 'identifier', 'id');
  if (direct) {
    return direct;
  }
  if (payload && typeof payload.device === 'object') {
    return pickString(payload.device, 'id', 'deviceId', 'identifier');
  }
  return null;
};

const extractDeviceName = (payload) => {
  const direct = pickString(payload, 'deviceName', 'name');
  if (direct) {
    return direct;
  }
  if (payload && typeof payload.device === 'object') {
    return pickString(payload.device, 'name', 'deviceName');
  }
  return null;
};

const extractDeviceType = (payload) => {
  const direct = pickString(payload, 'deviceType', 'type');
  if (direct) {
    return direct;
  }
  if (payload && typeof payload.device === 'object') {
    return pickString(payload.device, 'type', 'deviceType');
  }
  return null;
};

const extractDeviceLocation = (payload) => {
  const direct = pickString(payload, 'location', 'room');
  if (direct) {
    return direct;
  }
  if (payload && typeof payload.device === 'object') {
    return pickString(payload.device, 'location', 'room');
  }
  return null;
};

const extractTopicFromPayload = (payload, ...keys) => {
  const direct = pickString(payload, ...keys);
  if (direct) {
    return direct;
  }
  if (payload && typeof payload.device === 'object') {
    return pickString(payload.device, ...keys);
  }
  return null;
};

const buildSetUpdate = (fields) => {
  const update = {};
  Object.entries(fields).forEach(([key, value]) => {
    if (value !== undefined) {
      update[key] = value;
    }
  });
  return update;
};

const ensureUniqueDeviceName = async (name) => {
  const base = toTrimmedString(name) || 'Thiết bị';
  let candidate = base;
  let suffix = 2;
  while (await Device.exists({ name: candidate })) {
    candidate = `${base} ${suffix}`;
    suffix += 1;
  }
  return candidate;
};

const extractSensorIdentifier = (payload) => {
  const direct = pickString(payload, 'sensorId', 'sensor_id', 'id');
  if (direct) {
    return direct;
  }
  if (payload && typeof payload.sensor === 'object') {
    return pickString(payload.sensor, 'id', 'sensorId');
  }
  return null;
};

const extractSensorName = (payload) => {
  const direct = pickString(payload, 'sensorName', 'name');
  if (direct) {
    return direct;
  }
  if (payload && typeof payload.sensor === 'object') {
    return pickString(payload.sensor, 'name', 'sensorName');
  }
  return null;
};

const ensureDeviceForMessage = async (topic, payload, messageType) => {
  const identifier = extractDeviceIdentifier(payload);
  let device = await findDeviceByTopic(topic);

  const topicFromPayload =
    messageType === 'sensor'
      ? extractTopicFromPayload(payload, 'topicState', 'stateTopic')
      : extractTopicFromPayload(payload, 'topicTelemetry', 'telemetryTopic');
  const commandTopicFromPayload = extractTopicFromPayload(payload, 'topicCommand', 'commandTopic');

  const applyTopicUpdates = async (doc) => {
    const updates = {};

    if (!doc.topicState) {
      if (messageType !== 'sensor') {
        updates.topicState = topic;
      } else if (topicFromPayload) {
        updates.topicState = topicFromPayload;
      }
    }

    if (!doc.topicTelemetry) {
      if (messageType === 'sensor') {
        updates.topicTelemetry = topic;
      } else if (topicFromPayload) {
        updates.topicTelemetry = topicFromPayload;
      }
    }

    if (!doc.topicCommand && commandTopicFromPayload) {
      updates.topicCommand = commandTopicFromPayload;
    }

    if (!doc.identifier && identifier) {
      updates.identifier = identifier;
    }

    const payloadType = extractDeviceType(payload);
    if (!doc.type && payloadType) {
      updates.type = payloadType;
    }

    const payloadLocation = extractDeviceLocation(payload);
    if (!doc.location && payloadLocation) {
      updates.location = payloadLocation;
    }

    const hasUpdates = Object.keys(updates).length > 0;
    if (!hasUpdates) {
      return doc;
    }

    const updated = await Device.findByIdAndUpdate(doc._id, { $set: updates }, { new: true });
    if (updates.topicState || updates.topicTelemetry) {
      await registerDeviceTopics(updated);
    }
    return updated;
  };

  if (device) {
    return applyTopicUpdates(device);
  }

  if (identifier) {
    device = await Device.findOne({ identifier });
    if (device) {
      return applyTopicUpdates(device);
    }
  }

  if (!identifier) {
    return null;
  }

  const name = await ensureUniqueDeviceName(
    extractDeviceName(payload) || `Thiết bị ${identifier.slice(-4)}`,
  );

  const newDevice = await Device.create({
    identifier,
    name,
    type: extractDeviceType(payload) || 'generic',
    location: extractDeviceLocation(payload) || undefined,
    topicCommand: commandTopicFromPayload || undefined,
    topicState: messageType === 'sensor' ? topicFromPayload || undefined : topic,
    topicTelemetry: messageType === 'sensor' ? topic : topicFromPayload || undefined,
  });

  await registerDeviceTopics(newDevice);
  return newDevice;
};

const upsertSensorMetadata = async (deviceId, payload, recordedAt) => {
  const sensorId = extractSensorIdentifier(payload);
  if (!sensorId) {
    return;
  }

  const sensorName = extractSensorName(payload) || undefined;
  const metric = pickString(payload, 'metric', 'type') || undefined;
  const unit = pickString(payload, 'unit') || undefined;
  const lastValue = Object.prototype.hasOwnProperty.call(payload, 'value') ? payload.value : undefined;

  const setUpdate = buildSetUpdate({
    'sensors.$.name': sensorName,
    'sensors.$.metric': metric,
    'sensors.$.unit': unit,
    'sensors.$.lastValue': lastValue,
    'sensors.$.lastRecordedAt': recordedAt,
  });

  if (Object.keys(setUpdate).length > 0) {
    const result = await Device.updateOne(
      { _id: deviceId, 'sensors.sensorId': sensorId },
      { $set: setUpdate },
    );

    if (result.matchedCount > 0) {
      return;
    }
  }

  const sensorDocument = buildSetUpdate({
    sensorId,
    name: sensorName,
    metric,
    unit,
    lastValue,
    lastRecordedAt: recordedAt,
  });

  await Device.findByIdAndUpdate(deviceId, {
    $push: { sensors: sensorDocument },
  });
};

const getMqttUrl = () => process.env.MQTT_URL || 'mqtt://127.0.0.1:1883';

const resolveOptions = () => {
  const options = {};
  if (process.env.MQTT_USERNAME) {
    options.username = process.env.MQTT_USERNAME;
  }
  if (process.env.MQTT_PASSWORD) {
    options.password = process.env.MQTT_PASSWORD;
  }
  if (process.env.MQTT_CLIENT_ID) {
    options.clientId = process.env.MQTT_CLIENT_ID;
  }
  return options;
};

export const initializeMqtt = async () => {
  if (mqttClient) {
    return mqttClient;
  }

  mqttClient = mqtt.connect(getMqttUrl(), resolveOptions());

  mqttClient.on('connect', async () => {
    console.log('Connected to MQTT broker');
    await subscribeToDeviceTopics();
  });

  mqttClient.on('message', async (topic, payload) => {
    try {
      await handleIncomingMessage(topic, payload);
    } catch (error) {
      console.error('Failed to process MQTT message', error);
    }
  });

  mqttClient.on('error', (error) => {
    console.error('MQTT client error', error);
  });

  return mqttClient;
};

const subscribeToDeviceTopics = async () => {
  if (!mqttClient || mqttClient.disconnected) {
    return;
  }

  const devices = await Device.find();
  devices.forEach((device) => {
    [device.topicState, device.topicTelemetry]
      .filter((topic) => Boolean(topic))
      .forEach((topic) => {
        mqttClient.subscribe(topic, (error) => {
          if (error) {
            console.error(`Failed to subscribe to topic ${topic}`, error);
          } else {
            console.log(`Subscribed to topic ${topic}`);
          }
        });
      });
  });
};

export const registerDeviceTopics = async (device) => {
  if (!mqttClient || mqttClient.disconnected) {
    return;
  }

  [device.topicState, device.topicTelemetry]
    .filter((topic) => Boolean(topic))
    .forEach((topic) => {
      mqttClient.subscribe(topic, (error) => {
        if (error) {
          console.error(`Failed to subscribe to topic ${topic}`, error);
        }
      });
    });
};

async function findDeviceByTopic(topic) {
  return Device.findOne({
    $or: [{ topicState: topic }, { topicTelemetry: topic }],
  });
}

const handleIncomingMessage = async (topic, payloadBuffer) => {
  const payloadText = payloadBuffer.toString();
  let payload;
  try {
    payload = JSON.parse(payloadText);
  } catch (error) {
    payload = { raw: payloadText };
  }

  const messageType = payload.type || 'state';

  const device = await ensureDeviceForMessage(topic, payload, messageType);
  if (!device) {
    return;
  }

  if (messageType === 'sensor') {
    const recordedAt = payload.recordedAt ? new Date(payload.recordedAt) : new Date();
    const sensorId = extractSensorIdentifier(payload);
    const sensorName = extractSensorName(payload);

    await SensorReading.create({
      device: device._id,
      sensorId: sensorId || undefined,
      sensorName: sensorName || undefined,
      metric: payload.metric || 'value',
      value: payload.value,
      unit: payload.unit,
      recordedAt,
    });

    await upsertSensorMetadata(device._id, payload, recordedAt);
    await Device.findByIdAndUpdate(device._id, {
      $set: {
        lastTelemetry: payload,
        lastSeenAt: new Date(),
      },
    });
    return;
  }

  await Device.findByIdAndUpdate(device._id, {
    $set: {
      lastState: payload,
      lastSeenAt: new Date(),
    },
  });
};

export const publishCommand = async (device, payload) => {
  if (!mqttClient || mqttClient.disconnected) {
    throw new Error('MQTT client is not connected');
  }

  const topic = device.topicCommand;
  if (!topic) {
    throw new Error('Device does not have a command topic configured');
  }

  const message = typeof payload === 'string' ? payload : JSON.stringify(payload);

  mqttClient.publish(topic, message, (error) => {
    if (error) {
      console.error('Failed to publish MQTT message', error);
    }
  });
};

export const getMqttClient = () => mqttClient;
