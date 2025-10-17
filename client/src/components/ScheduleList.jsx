import PropTypes from 'prop-types';
import dayjs from 'dayjs';

const formatPayload = (payload) => {
  if (!payload) return '—';
  if (typeof payload === 'string') return payload;
  return JSON.stringify(payload);
};

const ScheduleList = ({ schedules }) => {
  if (!schedules.length) {
    return <p className="empty">No schedules created.</p>;
  }

  return (
    <table className="table">
      <thead>
        <tr>
          <th>Time</th>
          <th>Device</th>
          <th>Action</th>
          <th>Payload</th>
          <th>Status</th>
        </tr>
      </thead>
      <tbody>
        {schedules.map((schedule) => (
          <tr key={schedule._id}>
            <td>{dayjs(schedule.runAt).format('HH:mm:ss DD/MM/YYYY')}</td>
            <td>{schedule.deviceName || '—'}</td>
            <td>{schedule.action}</td>
            <td>{formatPayload(schedule.payload)}</td>
            <td>{schedule.status}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
};

ScheduleList.propTypes = {
  schedules: PropTypes.arrayOf(PropTypes.object),
};

ScheduleList.defaultProps = {
  schedules: [],
};

export default ScheduleList;
