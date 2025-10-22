import { useState } from 'react';
import PropTypes from 'prop-types';

const NaturalLanguageForm = ({ onSubmit, disabled }) => {
  const [prompt, setPrompt] = useState('Bật đèn phòng khách lúc 7 giờ tối');

  const handleSubmit = (event) => {
    event.preventDefault();
    if (!prompt.trim()) return;
    onSubmit(prompt.trim());
    setPrompt('');
  };

  return (
    <form className="nl-form" onSubmit={handleSubmit}>
      <textarea
        id="prompt"
        className="nl-form__input"
        value={prompt}
        onChange={(event) => setPrompt(event.target.value)}
        placeholder="Nhập yêu cầu cho trợ lý..."
        rows={2}
      />
      <button type="submit" disabled={disabled || !prompt.trim()}>
        Gửi
      </button>
    </form>
  );
};

NaturalLanguageForm.propTypes = {
  onSubmit: PropTypes.func.isRequired,
  disabled: PropTypes.bool,
};

NaturalLanguageForm.defaultProps = {
  disabled: false,
};

export default NaturalLanguageForm;
