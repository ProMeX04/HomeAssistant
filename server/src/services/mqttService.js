import mqtt from 'mqtt';
import Device from '../models/Device.js';
import SensorReading from '../models/SensorReading.js';

let mqttClient = null;

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

const findDeviceByTopic = async (topic) => {
  return Device.findOne({
    $or: [{ topicState: topic }, { topicTelemetry: topic }],
  });
};

const handleIncomingMessage = async (topic, payloadBuffer) => {
  const payloadText = payloadBuffer.toString();
  let payload;
  try {
    payload = JSON.parse(payloadText);
  } catch (error) {
    payload = { raw: payloadText };
  }

  const device = await findDeviceByTopic(topic);
  if (!device) {
    return;
  }

  const messageType = payload.type || 'state';

  if (messageType === 'sensor') {
    await SensorReading.create({
      device: device._id,
      metric: payload.metric || 'value',
      value: payload.value,
      unit: payload.unit,
      recordedAt: payload.recordedAt ? new Date(payload.recordedAt) : new Date(),
    });
    await Device.findByIdAndUpdate(device._id, {
      lastTelemetry: payload,
      lastSeenAt: new Date(),
    });
    return;
  }

  await Device.findByIdAndUpdate(device._id, {
    lastState: payload,
    lastSeenAt: new Date(),
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
