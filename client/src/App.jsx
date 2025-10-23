import { useEffect, useMemo, useState, useRef } from 'react';
import dayjs from 'dayjs';
import api from './api/client';
import DeviceList from './components/DeviceList';
import CommandLog from './components/CommandLog';
import NaturalLanguageForm from './components/NaturalLanguageForm';
import ScheduleList from './components/ScheduleList';
import './App.css';

const REFRESH_INTERVAL = 15000;

function App() {
  const [devices, setDevices] = useState([]);
  const [logs, setLogs] = useState([]);
  const [schedules, setSchedules] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [isSending, setIsSending] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const chatEndRef = useRef(null);

  const scrollToBottom = () => {
    chatEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  };

  const loadData = async () => {
    try {
      setLoading(true);
      const [devicesResponse, logsResponse, scheduleResponse] = await Promise.all([
        api.get('/devices'),
        api.get('/commands/logs'),
        api.get('/schedules'),
      ]);
      setDevices(devicesResponse.data.devices || []);
      setLogs(logsResponse.data.logs || []);
      setSchedules(scheduleResponse.data.schedules || []);
      setError(null);
    } catch (err) {
      console.error(err);
      setError(err.response?.data?.message || err.message);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
    const interval = setInterval(loadData, REFRESH_INTERVAL);
    return () => clearInterval(interval);
  }, []);

  useEffect(() => {
    scrollToBottom();
  }, [logs]);

  const handleDirectCommand = async (deviceId, action, payload) => {
    setIsSending(true);
    try {
      await api.post(`/devices/${deviceId}/commands`, {
        action,
        payload,
      });
      await loadData();
    } catch (err) {
      setError(err.response?.data?.message || err.message);
    } finally {
      setIsSending(false);
    }
  };

  const handleDeviceUpdate = async (deviceId, updates) => {
    setIsSending(true);
    try {
      await api.patch(`/devices/${deviceId}`, updates);
      await loadData();
    } catch (err) {
      setError(err.response?.data?.message || err.message);
      throw err;
    } finally {
      setIsSending(false);
    }
  };

  const handleNaturalLanguage = async (prompt) => {
    setIsSending(true);
    try {
      await api.post('/commands/natural', { prompt });
      await loadData();
    } catch (err) {
      setError(err.response?.data?.message || err.message);
    } finally {
      setIsSending(false);
    }
  };

  const handleAudioSubmit = async (audioBlob) => {
    setIsSending(true);
    try {
      const formData = new FormData();
      formData.append('audio', audioBlob, 'recording.webm');

      const response = await api.post('/commands/transcribe', formData, {
        headers: {
          'Content-Type': 'multipart/form-data',
        },
      });

      if (response.data.transcription) {
        await handleNaturalLanguage(response.data.transcription);
      }
    } catch (err) {
      setError(err.response?.data?.message || err.message);
    } finally {
      setIsSending(false);
    }
  };

  const latestUpdate = useMemo(() => {
    const timestamps = [
      ...devices.map((device) => device.updatedAt),
      ...logs.map((log) => log.createdAt),
    ]
      .filter(Boolean)
      .map((value) => dayjs(value).valueOf());

    if (timestamps.length === 0) {
      return null;
    }

    return dayjs(Math.max(...timestamps)).format('HH:mm:ss DD/MM/YYYY');
  }, [devices, logs]);

  return (
    <div className="app">
      {/* Sidebar Toggle Button */}
      <button 
        className="sidebar-toggle" 
        onClick={() => setSidebarOpen(!sidebarOpen)}
        aria-label="Toggle sidebar"
      >
        <svg width="24" height="24" viewBox="0 0 24 24" fill="none">
          <path d="M3 12h18M3 6h18M3 18h18" stroke="currentColor" strokeWidth="2" strokeLinecap="round"/>
        </svg>
      </button>

      <aside className={`app__sidebar ${sidebarOpen ? 'app__sidebar--open' : ''}`}>
        <div className="app__sidebar-header">
          <h1>🏠 Home Assistant</h1>
          {latestUpdate && <span className="app__timestamp">Cập nhật: {latestUpdate}</span>}
          {loading && <span className="app__status">Đang tải dữ liệu…</span>}
          {error && <p className="app__error">{error}</p>}
        </div>

        <section className="sidebar-section">
          <h2 className="sidebar-section__title">Thiết bị</h2>
          <DeviceList
            devices={devices}
            onSendCommand={handleDirectCommand}
            onUpdateDevice={handleDeviceUpdate}
            disabled={isSending}
          />
        </section>

        <section className="sidebar-section">
          <h2 className="sidebar-section__title">Lịch hẹn</h2>
          <ScheduleList schedules={schedules} />
        </section>
      </aside>

      <main className="app__main">
        <div className="chat-window">
          <CommandLog logs={logs} isLoading={isSending} />
          <div ref={chatEndRef} />
        </div>
        <div className="chat-input-container">
          <NaturalLanguageForm 
            onSubmit={handleNaturalLanguage}
            onAudioSubmit={handleAudioSubmit}
            disabled={isSending} 
          />
        </div>
      </main>
    </div>
  );
}

export default App;
