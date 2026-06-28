import { useMemo, useState } from "react";
import { api, openFile, reports, type Receipt, type StudentLink, type TeacherDashboard } from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, Notice, StatusPill, useAsync } from "../ui";
import { money, teacherNav } from "./teacherNav";

function studentName(students: StudentLink[], studentId: string): string {
  return students.find((student) => student.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}

export default function TeacherReceipts() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);
  const [status, setStatus] = useState("pending_review");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [actingId, setActingId] = useState<string | null>(null);
  const [modal, setModal] = useState<{ receipt: Receipt; action: "confirm" | "reject" } | null>(null);

  const filtered = useMemo(
    () => (receipts.data ?? []).filter((receipt) => status === "all" || receipt.status === status),
    [receipts.data, status],
  );

  async function act(receipt: Receipt, action: "confirm" | "reject") {
    setError(null);
    setNotice(null);
    setActingId(receipt.id);
    try {
      await api.post(`/payments/receipts/${receipt.id}/${action}`);
      setNotice(action === "confirm" ? "Чек подтверждён." : "Чек отклонён.");
      setModal(null);
      receipts.reload();
      dashboard.reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setActingId(null);
    }
  }

  return (
    <AppShell
      title="Чеки"
      subtitle="Проверка оплат учеников"
      navSection="Работа"
      navItems={teacherNav("receipts", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container">
        <div className="teacher-toolbar">
          <div className="segmented">
            {[
              ["pending_review", "На проверке"],
              ["confirmed", "Подтверждены"],
              ["rejected", "Отклонены"],
              ["all", "Все"],
            ].map(([value, label]) => (
              <button className={status === value ? "active" : ""} key={value} onClick={() => setStatus(value)}>
                {label}
              </button>
            ))}
          </div>
        </div>

        <Card title="Чеки учеников" icon="receipt_long">
          <ErrorMsg error={error || receipts.error} />
          <Notice text={notice} />
          {status === "pending_review" && filtered.length > 0 && (
            <div className="rule-banner">
              <Icon name="info" />
              <span>Баланс ученика меняется только после подтверждения чека преподавателем.</span>
            </div>
          )}
          {filtered.map((receipt) => (
            <div className="resource-row" key={receipt.id}>
              <div className="resource-icon"><Icon name="receipt_long" /></div>
              <div className="resource-main">
                <div className="summary-title">{studentName(students.data ?? [], receipt.student_id)}</div>
                <div className="summary-grid">
                  <span>{money(receipt.amount, receipt.currency)}</span>
                  <span>{receipt.submitted_at ? new Date(receipt.submitted_at).toLocaleString("ru-RU") : "-"}</span>
                  <span>file: {receipt.file_id.slice(0, 8)}</span>
                </div>
              </div>
              <div className="btn-group">
                <button className="small" onClick={() => openFile(receipt.file_id).catch((err) => setError((err as Error).message))}>Файл</button>
                {receipt.status === "pending_review" && (
                  <>
                    <button className="small primary" disabled={actingId === receipt.id} onClick={() => setModal({ receipt, action: "confirm" })}>Подтвердить</button>
                    <button className="small danger-button" disabled={actingId === receipt.id} onClick={() => setModal({ receipt, action: "reject" })}>Отклонить</button>
                  </>
                )}
                <StatusPill status={receipt.status} />
              </div>
            </div>
          ))}
          <ListState query={{ ...receipts, data: filtered }} empty="Чеки не найдены." />
        </Card>
      </div>

      {modal && (
        <ReceiptDecisionModal
          receipt={modal.receipt}
          action={modal.action}
          studentName={studentName(students.data ?? [], modal.receipt.student_id)}
          busy={actingId === modal.receipt.id}
          onClose={() => setModal(null)}
          onOpenFile={() => openFile(modal.receipt.file_id).catch((err) => setError((err as Error).message))}
          onSubmit={() => act(modal.receipt, modal.action)}
        />
      )}
    </AppShell>
  );
}

function ReceiptDecisionModal({
  receipt,
  action,
  studentName,
  busy,
  onClose,
  onOpenFile,
  onSubmit,
}: {
  receipt: Receipt;
  action: "confirm" | "reject";
  studentName: string;
  busy: boolean;
  onClose: () => void;
  onOpenFile: () => void;
  onSubmit: () => void;
}) {
  const confirming = action === "confirm";
  return (
    <div className="modal-overlay" onMouseDown={onClose}>
      <div className="modal-panel receipt-modal" onMouseDown={(event) => event.stopPropagation()}>
        <div className="modal-heading">
          <div>
            <h2>{confirming ? "Подтвердить чек" : "Отклонить чек"}</h2>
            <p>{studentName} · {money(receipt.amount, receipt.currency)}</p>
          </div>
          <button className="icon-button" type="button" onClick={onClose} title="Закрыть">
            <Icon name="close" />
          </button>
        </div>

        <button className="receipt-preview" type="button" onClick={onOpenFile}>
          <Icon name="description" />
          <span>Открыть файл чека</span>
          <strong>{receipt.file_id.slice(0, 8)}</strong>
        </button>

        <div className={"balance-effect " + (confirming ? "effect-confirm" : "effect-reject")}>
          <Icon name={confirming ? "check_circle" : "cancel"} />
          <div>
            <strong>{confirming ? "Оплата будет учтена" : "Долг не изменится"}</strong>
            <span>{confirming ? `Баланс ученика уменьшится на ${money(receipt.amount, receipt.currency)}.` : "Ученик увидит статус отклонения и сможет загрузить новый чек."}</span>
          </div>
        </div>

        <div className="modal-actions">
          <button type="button" onClick={onClose}>Отмена</button>
          <button className={confirming ? "primary" : "danger-button"} type="button" disabled={busy} onClick={onSubmit}>
            <Icon name={confirming ? "check_circle" : "cancel"} />
            {busy ? "Сохранение…" : confirming ? "Подтвердить" : "Отклонить"}
          </button>
        </div>
      </div>
    </div>
  );
}
