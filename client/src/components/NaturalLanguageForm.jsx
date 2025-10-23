git import { useState, useRef, useEffect } from 'react';
import PropTypes from 'prop-types';

const NaturalLanguageForm = ({ onSubmit, onAudioSubmit, disabled }) => {
  const [prompt, setPrompt] = useState('');
  const [isRecording, setIsRecording] = useState(false);
  const [recordingTime, setRecordingTime] = useState(0);
  const mediaRecorderRef = useRef(null);
  const audioChunksRef = useRef([]);
  const timerRef = useRef(null);

  useEffect(() => {
    return () => {
      if (timerRef.current) {
        clearInterval(timerRef.current);
      }
    };
  }, []);

  const handleSubmit = (event) => {
    event.preventDefault();
    if (!prompt.trim()) return;
    onSubmit(prompt.trim());
    setPrompt('');
  };

  const handleKeyDown = (event) => {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      handleSubmit(event);
    }
  };

  const startRecording = async () => {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const mediaRecorder = new MediaRecorder(stream);
      mediaRecorderRef.current = mediaRecorder;
      audioChunksRef.current = [];

      mediaRecorder.ondataavailable = (event) => {
        if (event.data.size > 0) {
          audioChunksRef.current.push(event.data);
        }
      };

      mediaRecorder.onstop = () => {
        const audioBlob = new Blob(audioChunksRef.current, { type: 'audio/webm' });
        onAudioSubmit(audioBlob);
        stream.getTracks().forEach(track => track.stop());
      };

      mediaRecorder.start();
      setIsRecording(true);
      setRecordingTime(0);

      timerRef.current = setInterval(() => {
        setRecordingTime((prev) => prev + 1);
      }, 1000);
    } catch (error) {
      console.error('Error accessing microphone:', error);
      alert('Không thể truy cập microphone. Vui lòng kiểm tra quyền truy cập.');
    }
  };

  const stopRecording = () => {
    if (mediaRecorderRef.current && isRecording) {
      mediaRecorderRef.current.stop();
      setIsRecording(false);
      if (timerRef.current) {
        clearInterval(timerRef.current);
      }
      setRecordingTime(0);
    }
  };

  const formatTime = (seconds) => {
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return `${mins}:${secs.toString().padStart(2, '0')}`;
  };

  return (
    <form className="nl-form" onSubmit={handleSubmit}>
      <div className="nl-form__wrapper">
        <textarea
          id="prompt"
          className="nl-form__input"
          value={prompt}
          onChange={(event) => setPrompt(event.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="Gửi tin nhắn tới Home Assistant..."
          rows={1}
          disabled={disabled || isRecording}
        />
        
        <div className="nl-form__actions">
          {isRecording ? (
            <button
              type="button"
              className="nl-form__btn nl-form__btn--recording"
              onClick={stopRecording}
              disabled={disabled}
              title="Dừng ghi âm"
            >
              <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor">
                <rect x="6" y="6" width="12" height="12" rx="2" />
              </svg>
              <span className="recording-time">{formatTime(recordingTime)}</span>
            </button>
          ) : (
            <button
              type="button"
              className="nl-form__btn nl-form__btn--mic"
              onClick={startRecording}
              disabled={disabled}
              title="Ghi âm"
            >
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor">
                <path d="M12 1a3 3 0 0 0-3 3v8a3 3 0 0 0 6 0V4a3 3 0 0 0-3-3z" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/>
                <path d="M19 10v2a7 7 0 0 1-14 0v-2M12 19v4M8 23h8" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/>
              </svg>
            </button>
          )}
          
          <button 
            type="submit" 
            className="nl-form__btn nl-form__btn--send"
            disabled={disabled || !prompt.trim() || isRecording}
            title="Gửi tin nhắn"
          >
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor">
              <path d="M22 2L11 13M22 2l-7 20-4-9-9-4 20-7z" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/>
            </svg>
          </button>
        </div>
      </div>
    </form>
  );
};

NaturalLanguageForm.propTypes = {
  onSubmit: PropTypes.func.isRequired,
  onAudioSubmit: PropTypes.func.isRequired,
  disabled: PropTypes.bool,
};

NaturalLanguageForm.defaultProps = {
  disabled: false,
};

export default NaturalLanguageForm;
