import dotenv from 'dotenv';
import app from './app.js';
import connectDatabase from './config/db.js';
import { initializeMqtt } from './services/mqttService.js';

dotenv.config();

const PORT = process.env.PORT || 5000;

async function bootstrap() {
  await connectDatabase();
  await initializeMqtt();

  app.listen(PORT, () => {
    console.log(`Server listening on port ${PORT}`);
  });
}

bootstrap().catch((error) => {
  console.error('Failed to bootstrap application', error);
  process.exit(1);
});
