import { useMemo, useState } from "react";
import { api, openFile, reports, type Receipt, type StudentDashboard } from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, StatusPill, useAsync } from "../ui";
import { money, studentNav } from "./studentNav";

function teacherName(dashboard: StudentDashboard | null, teacherId: string): string {
  return dashboard?.summaries.find((summary) => summary.teacher_id === teacherId)?.teacher_name ?? teacherId.slice(0, 8);
}

export default function StudentReceipts() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);
  const [status, setStatus] = useState("all");
  const [error, setError] = useState<string | null>(null);

  const activeAssignments = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.upcoming_lessons_count, 0) ?? 0;
  const filtered = useMemo(
    () => (receipts.data ?? []).filter((receipt) => status === "all" || receipt.status === status),
    [receipts.data, status],
  );

  return (
    <AppShell
      title="Мои чеки"
      subtitle="История отправленных чеков"
      navSection="Учёба"
      accent="student"
      navItems={studentNav("receipts", {
        lessons: upcomingLessons,
        assignments: activeAssignments,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container">
        <div className="teacher-toolbar">
          <div className="segmented">
            {[
              ["all", "Все"],
              ["pending_review", "На проверке"],
              ["confirmed", "Подтверждены"],
              ["rejected", "Отклонены"],
            ].map(([value, label]) => (
              <button className={status === value ? "active" : ""} key={value} onClick={() => setStatus(value)}>
                {label}
              </button>
            ))}
          </div>
        </div>

        <Card title="Чеки" icon="receipt_long">
          <ErrorMsg error={error || receipts.error} />
          {filtered.map((receipt) => (
            <div className="resource-row" key={receipt.id}>
              <div className="resource-icon"><Icon name="receipt_long" /></div>
              <div className="resource-main">
                <div className="summary-title">{money(receipt.amount, receipt.currency)}</div>
                <div className="summary-grid">
                  <span>{teacherName(dashboard.data, receipt.teacher_id)}</span>
                  <span>{receipt.submitted_at ? new Date(receipt.submitted_at).toLocaleString("ru-RU") : "-"}</span>
                  <span>file: {receipt.file_id.slice(0, 8)}</span>
                </div>
              </div>
              <div className="btn-group">
                <button className="small" onClick={() => openFile(receipt.file_id).catch((err) => setError((err as Error).message))}>Файл</button>
                <StatusPill status={receipt.status} />
              </div>
            </div>
          ))}
          <ListState query={{ ...receipts, data: filtered }} empty="Чеки не найдены." />
        </Card>
      </div>
    </AppShell>
  );
}
