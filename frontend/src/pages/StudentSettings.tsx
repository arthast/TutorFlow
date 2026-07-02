import { reports, type StudentDashboard } from "../api";
import { useAuth } from "../auth";
import { AppShell, Card, ChangePasswordCard, useAsync } from "../ui";
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
          <ChangePasswordCard />
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
