import mongoose from 'mongoose';

const commandLogSchema = new mongoose.Schema(
  {
    device: { type: mongoose.Schema.Types.ObjectId, ref: 'Device' },
    deviceName: { type: String },
    action: { type: String, required: true },
    payload: { type: mongoose.Schema.Types.Mixed },
    origin: { type: String, default: 'api' },
    naturalLanguage: { type: String },
    status: { type: String, default: 'pending' },
    runAt: { type: Date },
  },
  {
    timestamps: true,
  },
);

const CommandLog = mongoose.model('CommandLog', commandLogSchema);

export default CommandLog;
