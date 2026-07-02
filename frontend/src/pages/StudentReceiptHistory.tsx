import { Link } from "react-router-dom";
import { api, openFile, type FileMeta, type Receipt, type StudentDashboard } from "../api";
import { Button, EmptyState, Icon, StatusPill, money, useAsync } from "../ui";

export type StudentReceiptFilter = "all" | "pending_review" | "confirmed" | "rejected";

export function dateLabel(iso?: string): string {
  if (!iso) return "-";
  const date = new Date(iso);
  if (isNaN(date.getTime())) return "-";
  return date.toLocaleString("ru-RU", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
}

export function teacherName(dashboard: StudentDashboard | null, teacherId: string): string {
  return dashboard?.summaries.find((summary) => summary.teacher_id === teacherId)?.teacher_name ?? teacherId.slice(0, 8);
}

function statusTone(status: string): string {
  if (status === "confirmed") return "success";
  if (status === "rejected") return "danger";
  return "warning";
}

export function StudentReceiptHistory({
  receipts,
  dashboard,
  limit,
  emptyHint = "Чеков пока нет.",
  onError,
}: {
  receipts: Receipt[];
  dashboard: StudentDashboard | null;
  limit?: number;
  emptyHint?: string;
  onError: (message: string) => void;
}) {
  const visible = limit ? receipts.slice(0, limit) : receipts;

  if (visible.length === 0) {
    return <EmptyState icon="receipt_long" title="Чеков нет" hint={emptyHint} />;
  }

  return (
    <div className="student-receipt-list">
      {visible.map((receipt) => (
        <div className={"student-receipt-row student-receipt-row-" + statusTone(receipt.status)} key={receipt.id}>
          <div className="dash-icon lg tone-warning"><Icon name={receipt.status === "confirmed" ? "task_alt" : receipt.status === "rejected" ? "cancel" : "receipt_long"} /></div>
          <div className="student-receipt-main">
            <div className="student-receipt-title">
              <strong>{money(receipt.amount, receipt.currency)}</strong>
              <span>{teacherName(dashboard, receipt.teacher_id)}</span>
            </div>
            <ReceiptFileLink receipt={receipt} onError={onError} />
            {receipt.status === "rejected" && (
              <div className="receipt-reject-reason">
                {receipt.comment ? `Причина: ${receipt.comment}` : "Причина отклонения не передана текущим API."}
              </div>
            )}
          </div>
          <StatusPill status={receipt.status} />
        </div>
      ))}
      {limit && receipts.length > limit && (
        <Link className="button-like secondary has-icon student-history-more" to="/student/receipts">
          <Icon name="receipt_long" />Все чеки
        </Link>
      )}
    </div>
  );
}

function ReceiptFileLink({ receipt, onError }: { receipt: Receipt; onError: (message: string) => void }) {
  const meta = useAsync<FileMeta>(() => api.get(`/files/${receipt.file_id}`), [receipt.file_id]);
  const name = meta.data?.original_name || receipt.file_id.slice(0, 8);

  return (
    <Button
      className="receipt-file-link student-file-link"
      size="sm"
      variant="ghost"
      icon="description"
      title={meta.error ?? "Открыть файл"}
      onClick={() => openFile(receipt.file_id).catch((err) => onError((err as Error).message))}
    >
      <span>{name}</span>
      <em>{dateLabel(receipt.submitted_at)}</em>
    </Button>
  );
}
