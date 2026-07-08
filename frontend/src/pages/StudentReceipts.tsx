import { useMemo, useState } from "react";
import { api, reports, type Receipt, type StudentDashboard } from "../api";
import { AppShell, Card, ErrorMsg, ErrorState, SkeletonRows, Tabs, useAsync, type TabItem } from "../ui";
import { useDomainRefresh } from "../realtime";
import { studentNav } from "./studentNav";
import { StudentReceiptHistory, type StudentReceiptFilter } from "./StudentReceiptHistory";

export default function StudentReceipts() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);
  const [status, setStatus] = useState<StudentReceiptFilter>("all");
  const [error, setError] = useState<string | null>(null);

  const activeAssignments = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.upcoming_lessons_count, 0) ?? 0;

  useDomainRefresh(() => {
    dashboard.reload();
    receipts.reload();
  }, ["payment"]);

  const sortedReceipts = useMemo(
    () => [...(receipts.data ?? [])].sort((a, b) => new Date(b.submitted_at ?? 0).getTime() - new Date(a.submitted_at ?? 0).getTime()),
    [receipts.data],
  );
  const counts = useMemo(() => ({
    all: sortedReceipts.length,
    pending_review: sortedReceipts.filter((receipt) => receipt.status === "pending_review").length,
    confirmed: sortedReceipts.filter((receipt) => receipt.status === "confirmed").length,
    rejected: sortedReceipts.filter((receipt) => receipt.status === "rejected").length,
  }), [sortedReceipts]);
  const filtered = useMemo(
    () => sortedReceipts.filter((receipt) => status === "all" || receipt.status === status),
    [sortedReceipts, status],
  );

  const tabs: TabItem[] = [
    { key: "all", label: "Все", count: counts.all },
    { key: "pending_review", label: "На проверке", count: counts.pending_review },
    { key: "confirmed", label: "Подтверждены", count: counts.confirmed },
    { key: "rejected", label: "Отклонены", count: counts.rejected },
  ];

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
      <div className="container receipts-container">
        <Tabs items={tabs} active={status} onChange={(key) => setStatus(key as StudentReceiptFilter)} />
        <Card title="Чеки" icon="receipt_long">
          <ErrorMsg error={error || dashboard.error} />
          {receipts.loading && !receipts.data ? (
            <SkeletonRows count={5} />
          ) : receipts.error ? (
            <ErrorState error={receipts.error} onRetry={receipts.reload} />
          ) : (
            <StudentReceiptHistory
              receipts={filtered}
              dashboard={dashboard.data}
              emptyHint="В этой категории пока нет чеков."
              onError={setError}
            />
          )}
        </Card>
      </div>
    </AppShell>
  );
}
