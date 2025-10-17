import express from 'express';
import cors from 'cors';
import deviceRoutes from './routes/deviceRoutes.js';
import commandRoutes from './routes/commandRoutes.js';
import scheduleRoutes from './routes/scheduleRoutes.js';

const app = express();

app.use(cors({ origin: true, credentials: true }));
app.use(express.json());

app.get('/api/health', (_req, res) => {
  res.json({ status: 'ok' });
});

app.use('/api/devices', deviceRoutes);
app.use('/api/commands', commandRoutes);
app.use('/api/schedules', scheduleRoutes);

app.use((err, _req, res, _next) => {
  console.error(err);
  res.status(err.status || 500).json({
    message: err.message || 'Internal server error',
  });
});

export default app;
