import { useState, type FormEvent } from "react";
import { api, reports, type StudentDashboard } from "../api";
import { useAuth } from "../auth";
import { AppShell, Card, ErrorMsg, Icon, Notice, useAsync } from "../ui";
import { money, studentNav } from "./studentNav";

export default function StudentSettings() {
  const { user } = useAuth();
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const activeAssignments = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.upcoming_lessons_count, 0) ?? 0;

  return (
    <AppShell
      title="Настройки"
      subtitle="Профиль и доступ"
      navSection="Учёба"
      accent="student"
      navItems={studentNav("settings", {
        lessons: upcomingLessons,
        assignments: activeAssignments,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container">
        <div className="dashboard-grid">
          <Card title="Профиль" icon="person">
            <div className="summary-grid summary-grid-wide">
              <span>Имя: <strong>{user?.display_name ?? "-"}</strong></span>
              <span>Email: <strong>{user?.email ?? "-"}</strong></span>
              <span>Роль: <strong>Ученик</strong></span>
            </div>
          </Card>
          <PasswordCard />
          <Card title="Преподаватели" icon="group">
            {(dashboard.data?.summaries ?? []).map((summary) => (
              <div className="summary-row" key={summary.teacher_id}>
                <div>
                  <div className="summary-title">{summary.teacher_name || summary.teacher_id.slice(0, 8)}</div>
                  <div className="summary-grid">
                    <span>Долг: {money(summary.finance.debt_amount, summary.finance.currency)}</span>
                    <span>Ближайшие занятия: {summary.activity.upcoming_lessons_count}</span>
                    <span>Активные ДЗ: {summary.activity.active_assignments_count}</span>
                  </div>
                </div>
              </div>
            ))}
            {dashboard.data && dashboard.data.summaries.length === 0 && <p className="hint">Связанных преподавателей пока нет.</p>}
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
