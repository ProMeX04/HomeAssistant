import dayjs from 'dayjs';
import PropTypes from 'prop-types';

const formatState = (state) => {
  if (!state) return 'Unknown';
  if (typeof state === 'string') return state;
  if (typeof state === 'object') {
    return JSON.stringify(state);
  }
  return String(state);
};

const DeviceList = ({ devices, onSendCommand, disabled }) => {
  if (devices.length === 0) {
    return <p className="empty">No devices registered yet.</p>;
  }

  return (
    <div className="device-list">
      {devices.map((device) => (
        <div className="device-card" key={device._id}>
          <div className="device-card__header">
            <h3>{device.name}</h3>
            <span className="device-card__type">{device.type}</span>
          </div>
          <p className="device-card__meta">
            <strong>Location:</strong> {device.location || 'Unknown'}
          </p>
          <p className="device-card__meta">
            <strong>State:</strong> {formatState(device.lastState)}
          </p>
          {device.lastSeenAt && (
            <p className="device-card__meta">
              <strong>Last seen:</strong> {dayjs(device.lastSeenAt).format('HH:mm:ss DD/MM/YYYY')}
            </p>
          )}
          <div className="device-card__actions">
            <button type="button" disabled={disabled} onClick={() => onSendCommand(device._id, 'set_led', { command: 'set_led', state: 'on' })}>
              Turn on
            </button>
            <button type="button" disabled={disabled} onClick={() => onSendCommand(device._id, 'set_led', { command: 'set_led', state: 'off' })}>
              Turn off
            </button>
            <button type="button" disabled={disabled} onClick={() => onSendCommand(device._id, 'set_collection', { command: 'set_collection', state: 'off' })}>
              Pause collection
            </button>
          </div>
        </div>
      ))}
    </div>
  );
};

DeviceList.propTypes = {
  devices: PropTypes.arrayOf(PropTypes.object),
  onSendCommand: PropTypes.func.isRequired,
  disabled: PropTypes.bool,
};

DeviceList.defaultProps = {
  devices: [],
  disabled: false,
};

export default DeviceList;
