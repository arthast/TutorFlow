import { useState, type FormEvent } from "react";
import { Link } from "react-router-dom";
import { useAuth } from "../auth";
import { Card, ErrorMsg } from "../ui";

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
    <div className="auth-wrap">
      <Card title="Регистрация" icon="person_add">
        <ErrorMsg error={error} />
        <form onSubmit={onSubmit}>
          <div className="field">
            <label htmlFor="register-role">Я —</label>
            <select id="register-role" value={role} onChange={(e) => setRole(e.target.value as "teacher" | "student")}>
              <option value="teacher">Преподаватель</option>
              <option value="student">Ученик</option>
            </select>
          </div>
          <div className="field">
            <label htmlFor="register-name">Имя</label>
            <input id="register-name" value={displayName} onChange={(e) => setDisplayName(e.target.value)} required />
          </div>
          <div className="field">
            <label htmlFor="register-email">Email</label>
            <input id="register-email" type="email" value={email} onChange={(e) => setEmail(e.target.value)} required />
          </div>
          <div className="field">
            <label htmlFor="register-password">Пароль (мин. 8 символов)</label>
            <input id="register-password" type="password" value={password} onChange={(e) => setPassword(e.target.value)} minLength={8} required />
          </div>
          <button className="primary" type="submit" disabled={busy} style={{ width: "100%" }}>
            {busy ? "Создание…" : "Создать аккаунт"}
          </button>
        </form>
        <div className="center-link">
          Уже есть аккаунт? <Link to="/login">Войти</Link>
        </div>
      </Card>
    </div>
  );
}
