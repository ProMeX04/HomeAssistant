import { useEffect, useMemo, useState } from 'react';
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
      <header className="app__header">
        <h1>Home Assistant Dashboard</h1>
        {latestUpdate && <span className="app__timestamp">Last update: {latestUpdate}</span>}
      </header>

      {error && <div className="app__error">{error}</div>}

      <div className="app__content">
        <section className="panel">
          <header className="panel__header">
            <h2>Devices</h2>
            {loading && <span className="panel__status">Loading...</span>}
          </header>
          <DeviceList devices={devices} onSendCommand={handleDirectCommand} disabled={isSending} />
        </section>

        <section className="panel">
          <header className="panel__header">
            <h2>Natural language control</h2>
          </header>
          <NaturalLanguageForm onSubmit={handleNaturalLanguage} disabled={isSending} />
        </section>

        <section className="panel">
          <header className="panel__header">
            <h2>Upcoming schedules</h2>
          </header>
          <ScheduleList schedules={schedules} />
        </section>

        <section className="panel">
          <header className="panel__header">
            <h2>Command log</h2>
          </header>
          <CommandLog logs={logs} />
        </section>
      </div>
    </div>
  );
}

export default App;
