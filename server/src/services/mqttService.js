import mqtt from 'mqtt';
import Device from '../models/Device.js';
import SensorReading from '../models/SensorReading.js';

let mqttClient = null;

const splitTopics = (value) =>
  value
    .split(',')
    .map((topic) => topic.trim())
    .filter((topic) => topic.length > 0);

const getDiscoveryTopics = () => {
  const configured = toTrimmedString(process.env.MQTT_DISCOVERY_TOPICS);
  const topics = configured ? splitTopics(configured) : ['homeassistant/#'];
  console.log(`[MQTT DEBUG] Discovery topics:`, topics);
  return topics;
};

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
  console.log(`[MQTT DEBUG] ensureDeviceForMessage called with topic: ${topic}, messageType: ${messageType}`);

  const identifier = extractDeviceIdentifier(payload);
  console.log(`[MQTT DEBUG] Extracted device identifier: ${identifier}`);

  let device = await findDeviceByTopic(topic);
  console.log(`[MQTT DEBUG] Device found by topic:`, device ? device.name : 'null');

  const topicFromPayload =
    messageType === 'sensor'
      ? extractTopicFromPayload(payload, 'topicState', 'stateTopic')
      : extractTopicFromPayload(payload, 'topicTelemetry', 'telemetryTopic');
  const commandTopicFromPayload = extractTopicFromPayload(payload, 'topicCommand', 'commandTopic');

  console.log(`[MQTT DEBUG] Topics from payload - State: ${topicFromPayload}, Command: ${commandTopicFromPayload}`);

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
    console.log(`[MQTT DEBUG] No device identifier found, cannot create device`);
    return null;
  }

  console.log(`[MQTT DEBUG] Creating new device with identifier: ${identifier}`);

  const name = await ensureUniqueDeviceName(
    extractDeviceName(payload) || `Thiết bị ${identifier.slice(-4)}`,
  );

  console.log(`[MQTT DEBUG] Generated device name: ${name}`);

  const newDevice = await Device.create({
    identifier,
    name,
    type: extractDeviceType(payload) || 'generic',
    location: extractDeviceLocation(payload) || undefined,
    topicCommand: commandTopicFromPayload || undefined,
    topicState: messageType === 'sensor' ? topicFromPayload || undefined : topic,
    topicTelemetry: messageType === 'sensor' ? topic : topicFromPayload || undefined,
  });

  console.log(`[MQTT DEBUG] Created new device:`, newDevice.name);

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

const subscribeToTopics = (topics, { logOnSuccess = true } = {}) => {
  if (!mqttClient || mqttClient.disconnected) {
    console.log(`[MQTT DEBUG] Cannot subscribe - MQTT client not connected`);
    return;
  }

  console.log(`[MQTT DEBUG] Subscribing to topics:`, topics);

  topics
    .filter((topic) => Boolean(topic))
    .forEach((topic) => {
      console.log(`[MQTT DEBUG] Attempting to subscribe to: ${topic}`);
      mqttClient.subscribe(topic, (error) => {
        if (error) {
          console.error(`[MQTT DEBUG] Failed to subscribe to topic ${topic}`, error);
        } else if (logOnSuccess) {
          console.log(`[MQTT DEBUG] Successfully subscribed to topic ${topic}`);
        }
      });
    });
};

const subscribeToDiscoveryTopics = () => {
  const topics = getDiscoveryTopics();
  if (topics.length === 0) {
    return;
  }

  subscribeToTopics(topics, { logOnSuccess: false });
};

const subscribeToDeviceTopics = async () => {
  if (!mqttClient || mqttClient.disconnected) {
    return;
  }

  const devices = await Device.find();
  devices.forEach((device) => {
    subscribeToTopics([device.topicState, device.topicTelemetry]);
  });
};

export const registerDeviceTopics = async (device) => {
  subscribeToTopics([device.topicState, device.topicTelemetry], { logOnSuccess: false });
};

export const initializeMqtt = async () => {
  if (mqttClient) {
    console.log(`[MQTT DEBUG] MQTT client already exists`);
    return mqttClient;
  }

  const mqttUrl = getMqttUrl();
  console.log(`[MQTT DEBUG] Connecting to MQTT broker: ${mqttUrl}`);

  mqttClient = mqtt.connect(mqttUrl, resolveOptions());

  mqttClient.on('connect', async () => {
    console.log('[MQTT DEBUG] Connected to MQTT broker successfully');
    console.log('[MQTT DEBUG] Subscribing to discovery topics...');
    subscribeToDiscoveryTopics();
    console.log('[MQTT DEBUG] Subscribing to device topics...');
    await subscribeToDeviceTopics();
  });

  mqttClient.on('message', async (topic, payload) => {
    console.log(`[MQTT DEBUG] MQTT message received on topic: ${topic}`);
    try {
      await handleIncomingMessage(topic, payload);
    } catch (error) {
      console.error('[MQTT DEBUG] Failed to process MQTT message', error);
    }
  });

  mqttClient.on('error', (error) => {
    console.error('[MQTT DEBUG] MQTT client error', error);
  });

  mqttClient.on('disconnect', () => {
    console.log('[MQTT DEBUG] MQTT client disconnected');
  });

  return mqttClient;
};

async function findDeviceByTopic(topic) {
  return Device.findOne({
    $or: [{ topicState: topic }, { topicTelemetry: topic }],
  });
}

const handleIncomingMessage = async (topic, payloadBuffer) => {
  console.log(`[MQTT DEBUG] Received message on topic: ${topic}`);

  const payloadText = payloadBuffer.toString();
  console.log(`[MQTT DEBUG] Raw payload: ${payloadText}`);

  let payload;
  try {
    payload = JSON.parse(payloadText);
    console.log(`[MQTT DEBUG] Parsed payload:`, payload);
  } catch (error) {
    console.log(`[MQTT DEBUG] Failed to parse JSON payload:`, error.message);
    payload = { raw: payloadText };
  }

  const messageType = payload.type || 'state';
  console.log(`[MQTT DEBUG] Message type: ${messageType}`);

  const device = await ensureDeviceForMessage(topic, payload, messageType);
  console.log(`[MQTT DEBUG] Device found/created:`, device ? device.name : 'null');

  if (!device) {
    console.log(`[MQTT DEBUG] No device found/created, skipping message processing`);
    return;
  }

  if (messageType === 'sensor') {
    console.log(`[MQTT DEBUG] Processing sensor data`);

    const recordedAt = payload.recordedAt ? new Date(payload.recordedAt) : new Date();
    const sensorId = extractSensorIdentifier(payload);
    const sensorName = extractSensorName(payload);

    console.log(`[MQTT DEBUG] Sensor details - ID: ${sensorId}, Name: ${sensorName}, Metric: ${payload.metric}, Value: ${payload.value}`);

    await SensorReading.create({
      device: device._id,
      sensorId: sensorId || undefined,
      sensorName: sensorName || undefined,
      metric: payload.metric || 'value',
      value: payload.value,
      unit: payload.unit,
      recordedAt,
    });

    console.log(`[MQTT DEBUG] Created sensor reading for device: ${device.name}`);

    await upsertSensorMetadata(device._id, payload, recordedAt);
    await Device.findByIdAndUpdate(device._id, {
      $set: {
        lastTelemetry: payload,
        lastSeenAt: new Date(),
      },
    });
    return;
  }

  console.log(`[MQTT DEBUG] Processing state update for device: ${device.name}`);

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
