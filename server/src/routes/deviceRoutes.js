import { Router } from 'express';
import {
  listDevices,
  createDevice,
  updateDevice,
  sendDeviceCommand,
  getDeviceReadings,
} from '../controllers/deviceController.js';

const router = Router();

router.get('/', listDevices);
router.post('/', createDevice);
router.patch('/:id', updateDevice);
router.post('/:id/commands', sendDeviceCommand);
router.get('/:id/readings', getDeviceReadings);

export default router;
