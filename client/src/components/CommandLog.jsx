import PropTypes from 'prop-types';
import dayjs from 'dayjs';

const formatPayload = (payload) => {
  if (!payload) return '—';
  if (typeof payload === 'string') return payload;
  try {
    return JSON.stringify(payload);
  } catch (error) {
    return String(payload);
  }
};

const CommandLog = ({ logs }) => {
  if (!logs.length) {
    return <p className="empty">Chưa có lệnh nào được gửi.</p>;
  }

  return (
    <div className="chat-log">
      {logs.map((log) => {
        const role = log.origin?.toLowerCase() === 'user' ? 'user' : 'assistant';
        return (
          <div key={log._id} className={`chat-log__message chat-log__message--${role}`}>
            <div className="chat-log__bubble">
              <div className="chat-log__meta">
                <span>{role === 'user' ? 'Bạn' : log.origin || 'Hệ thống'}</span>
                <time dateTime={log.createdAt}>
                  {dayjs(log.createdAt).format('HH:mm:ss DD/MM/YYYY')}
                </time>
              </div>
              <div className="chat-log__content">
                <p className="chat-log__action">{log.action}</p>
                <p className="chat-log__payload">{formatPayload(log.payload)}</p>
                {log.deviceName && <p className="chat-log__device">Thiết bị: {log.deviceName}</p>}
                <p className="chat-log__status">Trạng thái: {log.status}</p>
              </div>
            </div>
          </div>
        );
      })}
    </div>
  );
};

CommandLog.propTypes = {
  logs: PropTypes.arrayOf(PropTypes.shape({
    _id: PropTypes.string.isRequired,
    action: PropTypes.string,
    payload: PropTypes.oneOfType([PropTypes.string, PropTypes.object, PropTypes.array]),
    origin: PropTypes.string,
    status: PropTypes.string,
    createdAt: PropTypes.string,
    deviceName: PropTypes.string,
  })),
};

CommandLog.defaultProps = {
  logs: [],
};

export default CommandLog;
