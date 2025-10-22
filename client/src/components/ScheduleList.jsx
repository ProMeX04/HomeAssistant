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

const ScheduleList = ({ schedules }) => {
  if (!schedules.length) {
    return <p className="empty">Chưa có lịch hẹn nào.</p>;
  }

  return (
    <ul className="schedule-list">
      {schedules.map((schedule) => (
        <li key={schedule._id} className="schedule-list__item">
          <div className="schedule-list__time">
            {dayjs(schedule.runAt).format('HH:mm DD/MM')}
          </div>
          <div className="schedule-list__details">
            <p className="schedule-list__device">{schedule.deviceName || 'Thiết bị không xác định'}</p>
            <p className="schedule-list__action">{schedule.action}</p>
            <p className="schedule-list__payload">{formatPayload(schedule.payload)}</p>
            <span className={`schedule-list__status schedule-list__status--${schedule.status?.toLowerCase() || 'pending'}`}>
              {schedule.status}
            </span>
          </div>
        </li>
      ))}
    </ul>
  );
};

ScheduleList.propTypes = {
  schedules: PropTypes.arrayOf(
    PropTypes.shape({
      _id: PropTypes.string.isRequired,
      runAt: PropTypes.string,
      deviceName: PropTypes.string,
      action: PropTypes.string,
      payload: PropTypes.oneOfType([PropTypes.string, PropTypes.object, PropTypes.array]),
      status: PropTypes.string,
    }),
  ),
};

ScheduleList.defaultProps = {
  schedules: [],
};

export default ScheduleList;
