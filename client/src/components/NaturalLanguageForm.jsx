import { useState } from 'react';
import PropTypes from 'prop-types';

const NaturalLanguageForm = ({ onSubmit, disabled }) => {
  const [prompt, setPrompt] = useState('Bật đèn phòng khách lúc 7 giờ tối');

  const handleSubmit = (event) => {
    event.preventDefault();
    if (!prompt.trim()) return;
    onSubmit(prompt.trim());
  };

  return (
    <form className="nl-form" onSubmit={handleSubmit}>
      <label htmlFor="prompt" className="nl-form__label">
        Enter a natural language command (Vietnamese or English):
      </label>
      <textarea
        id="prompt"
        className="nl-form__input"
        value={prompt}
        onChange={(event) => setPrompt(event.target.value)}
        placeholder="Ví dụ: Hãy tắt quạt phòng ngủ sau 10 phút"
        rows={3}
      />
      <button type="submit" disabled={disabled}>
        Send to assistant
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
