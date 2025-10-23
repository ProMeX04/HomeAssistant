import { Router } from 'express';
import multer from 'multer';
import {
  createCommand,
  listCommandLogs,
  naturalLanguageCommand,
  transcribeAudioCommand,
} from '../controllers/commandController.js';

const router = Router();

// Configure multer for audio file uploads
const storage = multer.diskStorage({
  destination: (req, file, cb) => {
    cb(null, 'uploads/');
  },
  filename: (req, file, cb) => {
    const uniqueSuffix = Date.now() + '-' + Math.round(Math.random() * 1E9);
    cb(null, 'audio-' + uniqueSuffix + '.webm');
  }
});

const upload = multer({
  storage,
  limits: {
    fileSize: 25 * 1024 * 1024, // 25MB limit
  },
  fileFilter: (req, file, cb) => {
    // Accept audio files
    if (file.mimetype.startsWith('audio/')) {
      cb(null, true);
    } else {
      cb(new Error('Only audio files are allowed'));
    }
  }
});

router.get('/logs', listCommandLogs);
router.post('/', createCommand);
router.post('/natural', naturalLanguageCommand);
router.post('/transcribe', upload.single('audio'), transcribeAudioCommand);

export default router;
