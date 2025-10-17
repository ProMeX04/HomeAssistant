import { Router } from 'express';
import { createScheduleEntry, getSchedules } from '../controllers/scheduleController.js';

const router = Router();

router.get('/', getSchedules);
router.post('/', createScheduleEntry);

export default router;
