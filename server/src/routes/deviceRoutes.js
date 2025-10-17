import { Router } from 'express';
import {
  listDevices,
  createDevice,
  sendDeviceCommand,
  getDeviceReadings,
} from '../controllers/deviceController.js';

const router = Router();

router.get('/', listDevices);
router.post('/', createDevice);
router.post('/:id/commands', sendDeviceCommand);
router.get('/:id/readings', getDeviceReadings);

export default router;
