import { GoogleGenAI, Type } from '@google/genai';
import dayjs from 'dayjs';

let client = null;

if (process.env.GEMINI_API_KEY) {
  client = new GoogleGenAI({
    apiKey: process.env.GEMINI_API_KEY,
  });
}

const functionDeclarations = [
  {
    name: 'dispatchDeviceCommand',
    description: 'Send an immediate command to a device',
    parameters: {
      type: Type.OBJECT,
      properties: {
        device: { type: Type.STRING, description: 'Device name that should receive the command' },
        action: { type: Type.STRING, description: 'Action such as on, off, toggle, set_value' },
        value: {
          type: Type.STRING,
          description: 'Optional value associated with the action (for example brightness level)',
        },
      },
      required: ['device', 'action'],
    },
  },
  {
    name: 'scheduleDeviceCommand',
    description: 'Schedule a device action for later execution',
    parameters: {
      type: Type.OBJECT,
      properties: {
        device: { type: Type.STRING, description: 'Device name' },
        action: { type: Type.STRING, description: 'Action name' },
        value: { type: Type.STRING, description: 'Optional action value' },
        runAt: {
          type: Type.STRING,
          description: 'ISO8601 timestamp for the moment the action should run',
        },
      },
      required: ['device', 'action', 'runAt'],
    },
  },
];

const SYSTEM_INSTRUCTION =
  'You are a smart home assistant that converts Vietnamese or English natural language requests into structured device actions. Use dispatchDeviceCommand for immediate commands and scheduleDeviceCommand for actions that happen in the future. The action should be a concise command such as on, off, toggle, set_value.';

const MODEL = process.env.GEMINI_MODEL || 'gemini-2.5-flash-lite';

const tryParseJSON = (value) => {
  if (!value) return undefined;
  if (typeof value === 'object') return value;
  try {
    return JSON.parse(value);
  } catch (error) {
    return undefined;
  }
};

const normalizeDeviceName = (name) => (name ? name.trim() : '');

const fallbackInterpretation = (prompt) => {
  const normalized = prompt.toLowerCase();
  let action = null;
  if (normalized.includes('turn on') || normalized.includes('bật')) {
    action = 'on';
  } else if (normalized.includes('turn off') || normalized.includes('tắt')) {
    action = 'off';
  } else if (normalized.includes('toggle') || normalized.includes('đảo')) {
    action = 'toggle';
  }

  const commandMatch = prompt.match(/(?:turn on|turn off|bật|tắt|toggle)\s+([\w\s]+)/i);
  const deviceName = commandMatch ? commandMatch[1].trim() : 'thiết bị';

  const scheduleMatch = prompt.match(/(?:at|lúc|vào)\s+([\w:\-\s]+)/i);
  const runAt = scheduleMatch ? new Date(scheduleMatch[1]) : null;

  if (runAt && !Number.isNaN(runAt.getTime())) {
    return {
      type: 'schedule',
      deviceName,
      action: action || 'toggle',
      payload: undefined,
      runAt: runAt.toISOString(),
    };
  }

  return {
    type: 'command',
    deviceName,
    action: action || 'toggle',
    payload: undefined,
  };
};

export const interpretCommand = async (prompt) => {
  if (!prompt || !prompt.trim()) {
    throw new Error('Prompt is required');
  }

  if (!client) {
    return fallbackInterpretation(prompt);
  }

  try {
    const contents = [
      {
        role: 'user',
        parts: [{ text: prompt }],
      },
    ];

    const config = {
      tools: [
        {
          functionDeclarations,
        },
      ],
      systemInstruction: [{ text: SYSTEM_INSTRUCTION }],
      thinkingConfig: { thinkingBudget: 8192 },
    };

    const response = await client.models.generateContent({
      model: MODEL,
      contents,
      config,
    });

    const candidate = response?.response?.candidates?.[0];
    const parts = candidate?.content?.parts || [];
    const functionCallPart = parts.find((part) => part.functionCall);

    if (!functionCallPart) {
      return fallbackInterpretation(prompt);
    }

    const { name, args } = functionCallPart.functionCall;
    const parsedArgs = tryParseJSON(args) || args;

    if (name === 'dispatchDeviceCommand') {
      return {
        type: 'command',
        deviceName: normalizeDeviceName(parsedArgs.device || parsedArgs.deviceName),
        action: parsedArgs.action,
        payload: parsedArgs.value ? { value: parsedArgs.value } : undefined,
      };
    }

    if (name === 'scheduleDeviceCommand') {
      const timestamp = parsedArgs.runAt;
      const parsedTimestamp = dayjs(timestamp);

      if (!parsedTimestamp.isValid()) {
        return {
          type: 'command',
          deviceName: normalizeDeviceName(parsedArgs.device || parsedArgs.deviceName),
          action: parsedArgs.action,
          payload: parsedArgs.value ? { value: parsedArgs.value } : undefined,
        };
      }

      return {
        type: 'schedule',
        deviceName: normalizeDeviceName(parsedArgs.device || parsedArgs.deviceName),
        action: parsedArgs.action,
        payload: parsedArgs.value ? { value: parsedArgs.value } : undefined,
        runAt: parsedTimestamp.toISOString(),
      };
    }
  } catch (error) {
    console.error('Gemini interpretation failed, using fallback', error);
  }

  return fallbackInterpretation(prompt);
};
