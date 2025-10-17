import PropTypes from 'prop-types';
import dayjs from 'dayjs';

const formatPayload = (payload) => {
  if (!payload) return '—';
  if (typeof payload === 'string') return payload;
  return JSON.stringify(payload);
};

const CommandLog = ({ logs }) => {
  if (!logs.length) {
    return <p className="empty">No commands have been sent yet.</p>;
  }

  return (
    <table className="table">
      <thead>
        <tr>
          <th>Time</th>
          <th>Device</th>
          <th>Action</th>
          <th>Payload</th>
          <th>Origin</th>
          <th>Status</th>
        </tr>
      </thead>
      <tbody>
        {logs.map((log) => (
          <tr key={log._id}>
            <td>{dayjs(log.createdAt).format('HH:mm:ss DD/MM/YYYY')}</td>
            <td>{log.deviceName || '—'}</td>
            <td>{log.action}</td>
            <td>{formatPayload(log.payload)}</td>
            <td>{log.origin}</td>
            <td>{log.status}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
};

CommandLog.propTypes = {
  logs: PropTypes.arrayOf(PropTypes.object),
};

CommandLog.defaultProps = {
  logs: [],
};

export default CommandLog;
