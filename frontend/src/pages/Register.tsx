import { useState, type FormEvent } from "react";
import { ApiError } from "../api";
import { useAuth } from "../auth";
import { Button, ErrorMsg, Field, Icon, PasswordInput, PasswordStrength } from "../ui";
import AuthLayout, { AuthLink } from "./AuthLayout";

export default function Register() {
  const { register } = useAuth();
  const [displayName, setDisplayName] = useState("");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [confirm, setConfirm] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const mismatch = confirm.length > 0 && confirm !== password;
  const canSubmit = displayName.trim().length > 0 && email.trim().length > 0 && password.length >= 8 && password === confirm;

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    setError(null);
    if (password.length < 8) {
      setError("Пароль — минимум 8 символов.");
      return;
    }
    if (password !== confirm) {
      setError("Пароли не совпадают.");
      return;
    }
    setBusy(true);
    try {
      await register({ email: email.trim(), password, role: "teacher", display_name: displayName.trim() });
    } catch (err) {
      if (err instanceof ApiError && err.status === 409) {
        setError("Этот email уже зарегистрирован. Войдите или используйте другой адрес.");
      } else {
        setError((err as Error).message);
      }
    } finally {
      setBusy(false);
    }
  }

  return (
    <AuthLayout
      title="Аккаунт преподавателя"
      subtitle="Создайте кабинет и приглашайте учеников."
      back={{ to: "/login", label: "Назад ко входу" }}
      footer={<>Уже есть аккаунт? <AuthLink to="/login">Войти</AuthLink></>}
    >
      <ErrorMsg error={error} />
      <form className="auth-form" onSubmit={onSubmit}>
        <Field label="Имя и фамилия">
          <div className="input-with-icon">
            <Icon name="badge" />
            <input value={displayName} onChange={(e) => setDisplayName(e.target.value)} placeholder="Елена Соколова" required autoFocus />
          </div>
        </Field>
        <Field label="Email">
          <div className="input-with-icon">
            <Icon name="mail" />
            <input type="email" value={email} onChange={(e) => setEmail(e.target.value)} placeholder="you@example.ru" autoComplete="username" required />
          </div>
        </Field>
        <Field label="Пароль">
          <PasswordInput value={password} onChange={setPassword} autoComplete="new-password" minLength={8} placeholder="Минимум 8 символов" required />
        </Field>
        {password.length > 0 && <PasswordStrength value={password} />}
        <Field label="Повторите пароль" error={mismatch ? "Пароли не совпадают" : null}>
          <PasswordInput value={confirm} onChange={setConfirm} autoComplete="new-password" invalid={mismatch} required />
        </Field>
        <Button variant="primary" type="submit" icon="person_add" loading={busy} disabled={!canSubmit} className="auth-submit">
          Создать аккаунт
        </Button>
      </form>
    </AuthLayout>
  );
}
