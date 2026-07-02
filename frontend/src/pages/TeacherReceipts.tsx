import { useMemo, useState } from "react";
import { ApiError, api, openFile, reports, type FileMeta, type Receipt, type StudentLink, type StudentSummary, type TeacherDashboard } from "../api";
import {
  AppShell,
  Avatar,
  Button,
  EmptyState,
  ErrorMsg,
  ErrorState,
  Icon,
  Modal,
  SkeletonRows,
  StatusPill,
  Tabs,
  Textarea,
  useAsync,
  useToast,
  type TabItem,
} from "../ui";
import { money as formatMoney, signedMoney as formatSignedBalance, teacherNav } from "./teacherNav";

type ReceiptFilter = "pending_review" | "confirmed" | "rejected" | "all";
type ReceiptAction = "confirm" | "reject";

function dateLabel(iso?: string): string {
  if (!iso) return "-";
  const date = new Date(iso);
  if (isNaN(date.getTime())) return "-";
  return date.toLocaleString("ru-RU", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
}

function studentName(students: StudentLink[], summaries: StudentSummary[], studentId: string): string {
  return summaries.find((summary) => summary.student_id === studentId)?.student_name
    || students.find((student) => student.student_id === studentId)?.display_name
    || studentId.slice(0, 8);
}

function studentBalance(summaries: StudentSummary[], studentId: string): number {
  return summaries.find((summary) => summary.student_id === studentId)?.finance.balance_amount ?? 0;
}

function statusAccent(status: string): string {
  if (status === "confirmed") return "success";
  if (status === "rejected") return "danger";
  return "warning";
}

export default function TeacherReceipts() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);
  const toast = useToast();
  const [status, setStatus] = useState<ReceiptFilter>("pending_review");
  const [error, setError] = useState<string | null>(null);
  const [actingId, setActingId] = useState<string | null>(null);
  const [modal, setModal] = useState<{ receipt: Receipt; action: ReceiptAction } | null>(null);

  const items = receipts.data ?? [];
  const summaries = dashboard.data?.students ?? [];
  const counts = useMemo(() => ({
    all: items.length,
    pending_review: items.filter((receipt) => receipt.status === "pending_review").length,
    confirmed: items.filter((receipt) => receipt.status === "confirmed").length,
    rejected: items.filter((receipt) => receipt.status === "rejected").length,
  }), [items]);

  const filtered = useMemo(
    () => items.filter((receipt) => status === "all" || receipt.status === status),
    [items, status],
  );

  const tabs: TabItem[] = [
    { key: "pending_review", label: "На проверке", count: counts.pending_review },
    { key: "confirmed", label: "Подтверждены", count: counts.confirmed },
    { key: "rejected", label: "Отклонены", count: counts.rejected },
    { key: "all", label: "Все", count: counts.all },
  ];

  function reloadAll() {
    receipts.reload();
    dashboard.reload();
  }

  async function act(receipt: Receipt, action: ReceiptAction, comment?: string) {
    setError(null);
    setActingId(receipt.id);
    try {
      if (action === "reject") {
        await api.post(`/payments/receipts/${receipt.id}/reject`, { comment });
      } else {
        await api.post(`/payments/receipts/${receipt.id}/confirm`);
      }
      toast({
        tone: action === "confirm" ? "success" : "warning",
        title: action === "confirm" ? "Чек подтверждён" : "Чек отклонён",
        body: action === "confirm" ? "Долг уменьшится после обновления финансовой сводки." : "Баланс ученика не изменился.",
      });
      setModal(null);
      reloadAll();
    } catch (err) {
      if (err instanceof ApiError && err.status === 409) {
        toast({ tone: "warning", title: "Чек уже обработан", body: err.message });
        setModal(null);
        reloadAll();
      } else {
        setError((err as Error).message);
        toast({ tone: "danger", title: "Не удалось обработать чек", body: (err as Error).message });
      }
    } finally {
      setActingId(null);
    }
  }

  return (
    <AppShell
      title="Чеки"
      subtitle="Подтверждение оплат"
      navSection="Работа"
      navItems={teacherNav("receipts", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container receipts-container">
        <Tabs items={tabs} active={status} onChange={(key) => setStatus(key as ReceiptFilter)} />

        {status === "pending_review" && filtered.length > 0 && (
          <div className="rule-banner">
            <Icon name="info" />
            <span>Чеки на проверке не уменьшают долг. Баланс меняется только после подтверждения преподавателем.</span>
          </div>
        )}

        <div className="receipt-list-head">
          <span>Ученик и файл</span>
          <span>Сумма</span>
          <span>Статус</span>
          <span></span>
        </div>

        <ErrorMsg error={error || dashboard.error || students.error} />
        {receipts.loading && !receipts.data ? (
          <div className="card"><SkeletonRows count={4} /></div>
        ) : receipts.error ? (
          <ErrorState error={receipts.error} onRetry={receipts.reload} />
        ) : filtered.length === 0 ? (
          <EmptyState icon="task_alt" title={status === "pending_review" ? "Чеков на проверке нет" : "Чеки не найдены"} hint="Все оплаты в этой категории уже обработаны." tone={status === "pending_review" ? "success" : undefined} />
        ) : (
          <div className="receipt-list">
            {filtered.map((receipt) => {
              const name = studentName(students.data ?? [], summaries, receipt.student_id);
              return (
                <div className={"receipt-row receipt-row-" + statusAccent(receipt.status)} key={receipt.id}>
                  <div className="receipt-person">
                    <Avatar name={name} tone="teacher" />
                    <div>
                      <strong>{name}</strong>
                      <ReceiptFileLine receipt={receipt} onError={setError} />
                    </div>
                  </div>
                  <strong className="receipt-amount">{formatMoney(receipt.amount, receipt.currency)}</strong>
                  <StatusPill status={receipt.status} />
                  <div className="btn-group receipt-actions">
                    {receipt.status === "pending_review" ? (
                      <>
                        <Button size="sm" variant="primary" loading={actingId === receipt.id} onClick={() => setModal({ receipt, action: "confirm" })}>Подтвердить</Button>
                        <Button size="sm" variant="danger" disabled={actingId === receipt.id} onClick={() => setModal({ receipt, action: "reject" })}>Отклонить</Button>
                      </>
                    ) : (
                      <span className="receipt-resolved">
                        <Icon name={receipt.status === "confirmed" ? "check_circle" : "cancel"} />
                        {receipt.reviewed_at ? dateLabel(receipt.reviewed_at) : "обработан"}
                      </span>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>

      {modal && (
        <ReceiptDecisionModal
          receipt={modal.receipt}
          action={modal.action}
          studentName={studentName(students.data ?? [], summaries, modal.receipt.student_id)}
          currentBalance={studentBalance(summaries, modal.receipt.student_id)}
          busy={actingId === modal.receipt.id}
          onClose={() => setModal(null)}
          onSubmit={(comment) => act(modal.receipt, modal.action, comment)}
        />
      )}
    </AppShell>
  );
}

function ReceiptFileLine({ receipt, onError }: { receipt: Receipt; onError: (error: string | null) => void }) {
  const meta = useAsync<FileMeta>(() => api.get(`/files/${receipt.file_id}`), [receipt.file_id]);
  const name = meta.data?.original_name || receipt.file_id.slice(0, 8);

  return (
    <button
      className="receipt-file-link"
      type="button"
      title={meta.error ?? "Открыть файл"}
      onClick={() => openFile(receipt.file_id).catch((err) => onError((err as Error).message))}
    >
      <Icon name="description" />
      <span>{name}</span>
      <em>{dateLabel(receipt.submitted_at)}</em>
      <Icon name="open_in_new" />
    </button>
  );
}

function ReceiptDecisionModal({
  receipt,
  action,
  studentName,
  currentBalance,
  busy,
  onClose,
  onSubmit,
}: {
  receipt: Receipt;
  action: ReceiptAction;
  studentName: string;
  currentBalance: number;
  busy: boolean;
  onClose: () => void;
  onSubmit: (comment?: string) => void;
}) {
  const [comment, setComment] = useState("");
  const [error, setError] = useState<string | null>(null);
  const confirming = action === "confirm";
  const nextBalance = currentBalance - receipt.amount;

  function submit() {
    if (!confirming && !comment.trim()) {
      setError("Укажите причину отклонения.");
      return;
    }
    onSubmit(confirming ? undefined : comment.trim());
  }

  return (
    <Modal
      title={confirming ? "Подтвердить оплату" : "Отклонить чек"}
      subtitle={`${studentName} · ${formatMoney(receipt.amount, receipt.currency)}`}
      onClose={onClose}
      footer={
        <>
          <Button variant="secondary" onClick={onClose}>Отмена</Button>
          <Button variant={confirming ? "primary" : "danger"} icon={confirming ? "check_circle" : "cancel"} loading={busy} onClick={submit}>
            {confirming ? "Подтвердить" : "Отклонить"}
          </Button>
        </>
      }
    >
      <ErrorMsg error={error} />
      <ReceiptFilePreview fileId={receipt.file_id} />
      <div className="balance-preview">
        <div>
          <span className="hint">Текущий долг</span>
          <strong>{formatSignedBalance(currentBalance, receipt.currency)}</strong>
        </div>
        <Icon name="arrow_forward" />
        <div>
          <span className="hint">После подтверждения</span>
          <strong className={nextBalance < 0 ? "amount-negative" : nextBalance > 0 ? "amount-positive" : ""}>
            {confirming ? formatSignedBalance(nextBalance, receipt.currency) : formatSignedBalance(currentBalance, receipt.currency)}
          </strong>
        </div>
      </div>
      <div className={"balance-effect " + (confirming ? "effect-confirm" : "effect-reject")}>
        <Icon name={confirming ? "check_circle" : "cancel"} />
        <div>
          <strong>{confirming ? "Долг уменьшится только сейчас" : "Долг не изменится"}</strong>
          <span>{confirming ? `Расчёт на фронте: ${formatSignedBalance(currentBalance, receipt.currency)} - ${formatMoney(receipt.amount, receipt.currency)}.` : "Отклонённый чек не создаёт payment-операцию."}</span>
        </div>
      </div>
      {!confirming && (
        <Textarea value={comment} onChange={(event) => setComment(event.target.value)} placeholder="Причина отклонения для ученика" autoFocus />
      )}
    </Modal>
  );
}

function ReceiptFilePreview({ fileId }: { fileId: string }) {
  const meta = useAsync<FileMeta>(() => api.get(`/files/${fileId}`), [fileId]);
  const name = meta.data?.original_name || fileId.slice(0, 8);
  const toast = useToast();
  return (
    <button
      className="receipt-preview"
      type="button"
      onClick={() => openFile(fileId).catch((err) => toast({ tone: "danger", title: "Файл не открылся", body: (err as Error).message }))}
    >
      <Icon name="description" />
      <span>{name}</span>
      <strong>открыть / скачать</strong>
    </button>
  );
}
