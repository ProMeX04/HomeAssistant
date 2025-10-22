import { useEffect, useState } from 'react';
import dayjs from 'dayjs';
import PropTypes from 'prop-types';

const formatState = (state) => {
  if (!state) return 'Không rõ';
  if (typeof state === 'string') return state;
  if (typeof state === 'object') {
    try {
      return JSON.stringify(state);
    } catch (error) {
      return String(state);
    }
  }
  return String(state);
};

const DeviceCard = ({ device, onSendCommand, onUpdateDevice, disabled }) => {
  const [isEditing, setIsEditing] = useState(false);
  const [name, setName] = useState(device.name);
  const [location, setLocation] = useState(device.location || '');
  const [saving, setSaving] = useState(false);
  const [localError, setLocalError] = useState(null);

  useEffect(() => {
    if (!isEditing) {
      setName(device.name);
      setLocation(device.location || '');
    }
  }, [device.name, device.location, isEditing]);

  const handleSubmit = async (event) => {
    event.preventDefault();
    const trimmedName = name.trim();
    if (trimmedName.length === 0) {
      setLocalError('Tên thiết bị là bắt buộc');
      return;
    }

    const trimmedLocation = location.trim();

    setSaving(true);
    setLocalError(null);
    try {
      await onUpdateDevice(device._id, {
        name: trimmedName,
        location: trimmedLocation.length > 0 ? trimmedLocation : null,
      });
      setIsEditing(false);
    } catch (error) {
      setLocalError(error.response?.data?.message || error.message || 'Không thể lưu thay đổi');
    } finally {
      setSaving(false);
    }
  };

  const handleCancel = () => {
    setIsEditing(false);
    setLocalError(null);
    setName(device.name);
    setLocation(device.location || '');
  };

  return (
    <div className={`device-item${isEditing ? ' device-item--editing' : ''}`}>
      <div className="device-item__header">
        <div className="device-item__info">
          <span className="device-item__name">{device.name}</span>
          <span className="device-item__meta">{device.location || 'Chưa có vị trí'}</span>
        </div>
        <button
          type="button"
          className="device-item__edit"
          onClick={() => {
            setIsEditing((value) => !value);
            setLocalError(null);
          }}
          disabled={disabled}
        >
          {isEditing ? 'Đóng' : 'Chỉnh sửa'}
        </button>
      </div>

      {isEditing ? (
        <form className="device-item__form" onSubmit={handleSubmit}>
          <label className="device-item__label">
            <span>Tên</span>
            <input
              type="text"
              value={name}
              onChange={(event) => setName(event.target.value)}
              disabled={saving}
              placeholder="Tên thiết bị"
            />
          </label>
          <label className="device-item__label">
            <span>Phạm vi / Vị trí</span>
            <input
              type="text"
              value={location}
              onChange={(event) => setLocation(event.target.value)}
              disabled={saving}
              placeholder="Ví dụ: Phòng khách"
            />
          </label>
          {localError && <p className="device-item__error">{localError}</p>}
          <div className="device-item__actions">
            <button type="submit" className="device-item__primary" disabled={saving}>
              {saving ? 'Đang lưu…' : 'Lưu'}
            </button>
            <button type="button" onClick={handleCancel} className="device-item__ghost" disabled={saving}>
              Hủy
            </button>
          </div>
        </form>
      ) : (
        <div className="device-item__body">
          <div className="device-item__state">
            <span className="device-item__label-text">Trạng thái</span>
            <p>{formatState(device.lastState)}</p>
          </div>
          {device.lastSeenAt && (
            <div className="device-item__state">
              <span className="device-item__label-text">Lần cuối hoạt động</span>
              <p>{dayjs(device.lastSeenAt).format('HH:mm:ss DD/MM/YYYY')}</p>
            </div>
          )}
          <div className="device-item__quick-actions">
            <button
              type="button"
              disabled={disabled}
              onClick={() => onSendCommand(device._id, 'set_led', { command: 'set_led', state: 'on' })}
            >
              Bật đèn
            </button>
            <button
              type="button"
              disabled={disabled}
              onClick={() => onSendCommand(device._id, 'set_led', { command: 'set_led', state: 'off' })}
            >
              Tắt đèn
            </button>
            <button
              type="button"
              disabled={disabled}
              onClick={() =>
                onSendCommand(device._id, 'set_collection', { command: 'set_collection', state: 'off' })
              }
            >
              Tạm dừng thu thập
            </button>
          </div>
        </div>
      )}
    </div>
  );
};

DeviceCard.propTypes = {
  device: PropTypes.shape({
    _id: PropTypes.string.isRequired,
    name: PropTypes.string.isRequired,
    type: PropTypes.string,
    location: PropTypes.string,
    lastState: PropTypes.oneOfType([PropTypes.string, PropTypes.object, PropTypes.array]),
    lastSeenAt: PropTypes.string,
  }).isRequired,
  onSendCommand: PropTypes.func.isRequired,
  onUpdateDevice: PropTypes.func.isRequired,
  disabled: PropTypes.bool,
};

DeviceCard.defaultProps = {
  disabled: false,
};

const DeviceList = ({ devices, onSendCommand, onUpdateDevice, disabled }) => {
  if (devices.length === 0) {
    return <p className="empty">Chưa có thiết bị nào.</p>;
  }

  return (
    <div className="device-list">
      {devices.map((device) => (
        <DeviceCard
          key={device._id}
          device={device}
          onSendCommand={onSendCommand}
          onUpdateDevice={onUpdateDevice}
          disabled={disabled}
        />
      ))}
    </div>
  );
};

DeviceList.propTypes = {
  devices: PropTypes.arrayOf(PropTypes.object),
  onSendCommand: PropTypes.func.isRequired,
  onUpdateDevice: PropTypes.func.isRequired,
  disabled: PropTypes.bool,
};

DeviceList.defaultProps = {
  devices: [],
  disabled: false,
};

export default DeviceList;
