import { useState, type FormEvent } from "react";
import { ApiError } from "../api";
import { useAuth } from "../auth";
import { Button, ErrorMsg, Field, Icon, PasswordInput } from "../ui";
import AuthLayout, { AuthLink } from "./AuthLayout";

export default function Login() {
  const { login } = useAuth();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await login(email.trim(), password);
    } catch (err) {
      if (err instanceof ApiError && (err.status === 400 || err.status === 401)) {
        setError("Неверный email или пароль.");
      } else {
        setError((err as Error).message);
      }
    } finally {
      setBusy(false);
    }
  }

  return (
    <AuthLayout
      title="Вход в кабинет"
      subtitle="Рады видеть снова. Введите данные для входа."
      footer={<>Нет аккаунта? <AuthLink to="/register">Создать аккаунт преподавателя</AuthLink></>}
    >
      <ErrorMsg error={error} />
      <form className="auth-form" onSubmit={onSubmit}>
        <Field label="Email">
          <div className="input-with-icon">
            <Icon name="mail" />
            <input
              id="login-email"
              type="email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              autoComplete="username"
              placeholder="you@example.ru"
              required
              autoFocus
            />
          </div>
        </Field>
        <Field label="Пароль">
          <PasswordInput id="login-password" value={password} onChange={setPassword} autoComplete="current-password" required />
        </Field>
        <Button variant="primary" type="submit" icon="login" loading={busy} className="auth-submit">
          Войти
        </Button>
      </form>

      <div className="info-note auth-info">
        <Icon name="info" />
        <span>Ученики не регистрируются сами — преподаватель создаёт аккаунт и выдаёт временный пароль.</span>
      </div>
    </AuthLayout>
  );
}
