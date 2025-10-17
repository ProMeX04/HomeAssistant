import mongoose from 'mongoose';

const deviceSchema = new mongoose.Schema(
  {
    name: { type: String, required: true, unique: true },
    type: { type: String, default: 'generic' },
    location: { type: String },
    topicCommand: { type: String, required: true },
    topicState: { type: String },
    topicTelemetry: { type: String },
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
