import mongoose from 'mongoose';

const deviceSchema = new mongoose.Schema(
  {
    identifier: { type: String, unique: true, sparse: true },
    name: { type: String, required: true, unique: true },
    type: { type: String, default: 'generic' },
    location: { type: String },
    topicCommand: { type: String },
    topicState: { type: String },
    topicTelemetry: { type: String },
    sensors: [
      {
        sensorId: { type: String, required: true },
        name: { type: String },
        metric: { type: String },
        unit: { type: String },
        lastValue: { type: mongoose.Schema.Types.Mixed },
        lastRecordedAt: { type: Date },
      },
    ],
    lastState: { type: mongoose.Schema.Types.Mixed },
    lastTelemetry: { type: mongoose.Schema.Types.Mixed },
    lastSeenAt: { type: Date },
  },
  {
    timestamps: true,
  },
);

const Device = mongoose.model('Device', deviceSchema);

export default Device;
