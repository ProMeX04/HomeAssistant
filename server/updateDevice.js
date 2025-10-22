import mongoose from 'mongoose';
import Device from './src/models/Device.js';

const MONGODB_URI = process.env.MONGODB_URI || 'mongodb://127.0.0.1:27017/homeassistant';

async function updateDevice() {
  try {
    await mongoose.connect(MONGODB_URI);
    console.log('Connected to MongoDB');

    const result = await Device.findByIdAndUpdate(
      '68f92638fc217a6db6180664',
      {
        $set: {
          topicCommand: 'homeassistant/automation/command',
          topicState: 'homeassistant/automation/state',
          location: 'Living Room'
        }
      },
      { new: true }
    );

    console.log('Device updated:', result);
    await mongoose.connection.close();
  } catch (error) {
    console.error('Error updating device:', error);
    process.exit(1);
  }
}

updateDevice();