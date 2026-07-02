import { reports, type TeacherDashboard } from "../api";
import { useAuth } from "../auth";
import { AppShell, Card, ChangePasswordCard, useAsync } from "../ui";
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
          <ChangePasswordCard />
          <Card title="Уведомления" icon="notifications">
            <p className="hint">Скелет раздела. Настройки уведомлений появятся после backend-контракта.</p>
          </Card>
        </div>
      </div>
    </AppShell>
  );
}
