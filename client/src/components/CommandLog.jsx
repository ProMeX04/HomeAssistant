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

const CommandLog = ({ logs, isLoading }) => {
  if (!logs.length && !isLoading) {
    return (
      <div className="empty-state">
        <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
          <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z" strokeLinecap="round" strokeLinejoin="round"/>
        </svg>
        <p>Bắt đầu trò chuyện với Home Assistant</p>
        <p className="empty-state__subtitle">Gõ tin nhắn hoặc sử dụng microphone để điều khiển thiết bị</p>
      </div>
    );
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
      
      {isLoading && (
        <div className="chat-log__message chat-log__message--assistant">
          <div className="chat-log__bubble">
            <div className="chat-log__loading">
              <span className="dot"></span>
              <span className="dot"></span>
              <span className="dot"></span>
            </div>
          </div>
        </div>
      )}
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
  isLoading: PropTypes.bool,
};

CommandLog.defaultProps = {
  logs: [],
  isLoading: false,
};

export default CommandLog;
