import { useState, type FormEvent } from "react";
import { useAuth } from "../auth";
import { ErrorMsg, Icon } from "../ui";
import AuthLayout, { AuthLink } from "./AuthLayout";

export default function Register() {
  const { register } = useAuth();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [displayName, setDisplayName] = useState("");
  const [role, setRole] = useState<"teacher" | "student">("teacher");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await register({ email, password, role, display_name: displayName });
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <AuthLayout
      title="Регистрация"
      subtitle="Создайте рабочий аккаунт"
      footer={<>Уже есть аккаунт? <AuthLink to="/login">Войти</AuthLink></>}
    >
        <ErrorMsg error={error} />
        <form className="auth-form" onSubmit={onSubmit}>
          <div className="field">
            <label htmlFor="register-role">Я —</label>
            <div className="role-segment">
              <button className={role === "teacher" ? "active" : ""} type="button" onClick={() => setRole("teacher")}><Icon name="school" />Преподаватель</button>
              <button className={role === "student" ? "active" : ""} type="button" onClick={() => setRole("student")}><Icon name="person" />Ученик</button>
            </div>
          </div>
          <div className="field">
            <label htmlFor="register-name">Имя</label>
            <div className="input-with-icon"><Icon name="badge" /><input id="register-name" value={displayName} onChange={(e) => setDisplayName(e.target.value)} required /></div>
          </div>
          <div className="field">
            <label htmlFor="register-email">Email</label>
            <div className="input-with-icon"><Icon name="mail" /><input id="register-email" type="email" value={email} onChange={(e) => setEmail(e.target.value)} required /></div>
          </div>
          <div className="field">
            <label htmlFor="register-password">Пароль (мин. 8 символов)</label>
            <div className="input-with-icon"><Icon name="lock" /><input id="register-password" type="password" value={password} onChange={(e) => setPassword(e.target.value)} minLength={8} required /></div>
          </div>
          <button className="primary auth-submit" type="submit" disabled={busy}>
            <Icon name="person_add" />
            {busy ? "Создание..." : "Создать аккаунт"}
          </button>
        </form>
    </AuthLayout>
  );
}
