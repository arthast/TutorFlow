import { useState, type FormEvent } from "react";
import { useAuth } from "../auth";
import { ErrorMsg, Icon } from "../ui";
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
      await login(email, password);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <AuthLayout
      title="Вход"
      subtitle="Введите email и пароль"
      footer={<>Нет аккаунта? <AuthLink to="/register">Зарегистрироваться</AuthLink></>}
    >
        <ErrorMsg error={error} />
        <form className="auth-form" onSubmit={onSubmit}>
          <div className="field">
            <label htmlFor="login-email">Email</label>
            <div className="input-with-icon"><Icon name="mail" /><input id="login-email" type="email" value={email} onChange={(e) => setEmail(e.target.value)} required /></div>
          </div>
          <div className="field">
            <label htmlFor="login-password">Пароль</label>
            <div className="input-with-icon"><Icon name="lock" /><input id="login-password" type="password" value={password} onChange={(e) => setPassword(e.target.value)} required /></div>
          </div>
          <button className="primary auth-submit" type="submit" disabled={busy}>
            <Icon name="login" />
            {busy ? "Вход..." : "Войти"}
          </button>
        </form>
    </AuthLayout>
  );
}
