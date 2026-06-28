import { useState, type FormEvent } from "react";
import { api, reports, type TeacherDashboard } from "../api";
import { useAuth } from "../auth";
import { AppShell, Card, ErrorMsg, Icon, Notice, useAsync } from "../ui";
import { teacherNav } from "./teacherNav";

export default function TeacherSettings() {
  const { user } = useAuth();
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);

  return (
    <AppShell
      title="Настройки"
      subtitle="Профиль и доступ"
      navSection="Работа"
      navItems={teacherNav("settings", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container">
        <div className="dashboard-grid">
          <Card title="Профиль" icon="person">
            <div className="summary-grid summary-grid-wide">
              <span>Имя: <strong>{user?.display_name ?? "-"}</strong></span>
              <span>Email: <strong>{user?.email ?? "-"}</strong></span>
              <span>Роль: <strong>Преподаватель</strong></span>
            </div>
          </Card>
          <PasswordCard />
          <Card title="Уведомления" icon="notifications">
            <p className="hint">Скелет раздела. Настройки уведомлений появятся после backend-контракта.</p>
          </Card>
        </div>
      </div>
    </AppShell>
  );
}

function PasswordCard() {
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function change(event: FormEvent) {
    event.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      await api.post("/auth/change-password", {
        current_password: current,
        new_password: next,
      });
      setNotice("Пароль обновлён.");
      setCurrent("");
      setNext("");
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Сменить пароль" icon="lock_reset">
      <ErrorMsg error={error} />
      <Notice text={notice} />
      <form onSubmit={change}>
        <div className="field"><label>Текущий пароль<input type="password" value={current} onChange={(event) => setCurrent(event.target.value)} required /></label></div>
        <div className="field"><label>Новый пароль<input type="password" value={next} onChange={(event) => setNext(event.target.value)} minLength={8} required /></label></div>
        <button className="primary" type="submit" disabled={busy}>
          <Icon name="lock_reset" />
          {busy ? "Обновление..." : "Обновить пароль"}
        </button>
      </form>
    </Card>
  );
}
