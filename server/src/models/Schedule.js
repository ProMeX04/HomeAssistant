import mongoose from 'mongoose';

const scheduleSchema = new mongoose.Schema(
  {
    device: { type: mongoose.Schema.Types.ObjectId, ref: 'Device', required: true },
    deviceName: { type: String },
    action: { type: String, required: true },
    payload: { type: mongoose.Schema.Types.Mixed },
    runAt: { type: Date, required: true },
    origin: { type: String, default: 'api' },
    naturalLanguage: { type: String },
    status: { type: String, default: 'scheduled' },
  },
  {
    timestamps: true,
  },
);

const Schedule = mongoose.model('Schedule', scheduleSchema);

export default Schedule;
