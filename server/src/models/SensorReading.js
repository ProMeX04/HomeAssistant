import mongoose from 'mongoose';

const sensorReadingSchema = new mongoose.Schema(
  {
    device: { type: mongoose.Schema.Types.ObjectId, ref: 'Device', required: true },
    metric: { type: String, required: true },
    value: { type: mongoose.Schema.Types.Mixed, required: true },
    unit: { type: String },
    recordedAt: { type: Date, default: Date.now },
  },
  {
    timestamps: true,
  },
);

const SensorReading = mongoose.model('SensorReading', sensorReadingSchema);

export default SensorReading;
