import { Router } from 'express';
import {
  createCommand,
  listCommandLogs,
  naturalLanguageCommand,
} from '../controllers/commandController.js';

const router = Router();

router.get('/logs', listCommandLogs);
router.post('/', createCommand);
router.post('/natural', naturalLanguageCommand);

export default router;
