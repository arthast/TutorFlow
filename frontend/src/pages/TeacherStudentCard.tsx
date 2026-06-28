import { useMemo, useState, type FormEvent } from "react";
import { Link, Navigate, useParams } from "react-router-dom";
import {
  api,
  type Assignment,
  type Balance,
  type Lesson,
  type Receipt,
  type StudentLink,
  type StudentSummary,
  type TeacherDashboard,
  type Transaction,
} from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, Notice, StatusPill, fmtDate, useAsync } from "../ui";
import { initials, money, teacherNav } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;

export default function TeacherStudentCard() {
  const { studentId } = useParams();
  const dashboard = useAsync<TeacherDashboard>(() => api.get("/dashboard/teacher"), []);
  const student = useAsync<StudentLink>(() => api.get(`/students/${studentId}`), [studentId]);
  const summary = useAsync<StudentSummary>(() => api.get(`/students/${studentId}/summary`), [studentId]);
  const balance = useAsync<Balance>(() => api.get(`/students/${studentId}/balance`), [studentId]);
  const transactions = useAsync<Transaction[]>(() => api.get(`/students/${studentId}/transactions`), [studentId]);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts?status=pending_review"), []);

  if (!studentId) return <Navigate to="/teacher" replace />;

  const studentLessons = (lessons.data ?? []).filter((lesson) => lesson.student_id === studentId);
  const studentAssignments = (assignments.data ?? []).filter((assignment) => assignment.student_id === studentId);
  const studentReceipts = (receipts.data ?? []).filter((receipt) => receipt.student_id === studentId);
  const displayName = student.data?.display_name ?? summary.data?.student_name ?? "Ученик";

  return (
    <AppShell
      title={displayName}
      subtitle="Карточка ученика"
      navSection="Работа"
      navItems={teacherNav("students", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={
        <Link className="primary-action" to="/teacher/lessons">
          <Icon name="calendar_month" />
          <span>К расписанию</span>
        </Link>
      }
    >
      <div className="container">
        <StudentHeader student={student} summary={summary} />

        <div className="metrics">
          <Metric icon="account_balance_wallet" label="Долг" value={money(summary.data?.finance.debt_amount ?? balance.data?.balance, balance.data?.currency ?? summary.data?.finance.currency)} />
          <Metric icon="payments" label="Переплата" value={money(summary.data?.finance.overpaid_amount, summary.data?.finance.currency)} />
          <Metric icon="receipt_long" label="Чеки на проверке" value={`${summary.data?.finance.pending_receipts_count ?? studentReceipts.length} / ${money(summary.data?.finance.pending_receipts_amount, summary.data?.finance.currency)}`} />
          <Metric icon="calendar_month" label="Ближайшие занятия" value={summary.data?.activity.upcoming_lessons_count ?? studentLessons.filter((lesson) => lesson.status === "scheduled").length} />
        </div>

        <div className="dashboard-grid">
          <div className="dashboard-column">
            <Card title="Журнал операций" icon="receipt_long">
              {(transactions.data ?? []).map((transaction) => (
                <div className="transaction-row" key={transaction.id}>
                  <div className="transaction-icon"><Icon name={iconForTransaction(transaction.type)} /></div>
                  <div>
                    <div className="summary-title">{labelForTransaction(transaction.type)}</div>
                    <div className="muted">{transaction.comment || fmtDate(transaction.created_at) || "—"}</div>
                  </div>
                  <strong className={transaction.amount >= 0 ? "amount-positive" : "amount-negative"}>
                    {transaction.amount > 0 ? "+" : ""}{money(transaction.amount, transaction.currency)}
                  </strong>
                </div>
              ))}
              <ListState query={transactions} empty="Операций пока нет." />
            </Card>

            <Card title="Занятия ученика" icon="calendar_month">
              {studentLessons.map((lesson) => (
                <div className="row" key={lesson.id}>
                  <span>{lesson.topic || "Занятие"} · {fmtDate(lesson.starts_at)}</span>
                  <StatusPill status={lesson.status} />
                </div>
              ))}
              <ListState query={{ ...lessons, data: studentLessons }} empty="Занятий пока нет." />
            </Card>
          </div>

          <div className="dashboard-column">
            <CorrectionCard studentId={studentId} balance={balance} transactions={transactions} dashboard={dashboard} summary={summary} />
            <Card title="Домашние задания" icon="assignment">
              {studentAssignments.map((assignment) => (
                <div className="row" key={assignment.id}>
                  <Link to={`/teacher/assignments/${assignment.id}/review`}>{assignment.title}</Link>
                  <StatusPill status={assignment.status} />
                </div>
              ))}
              <ListState query={{ ...assignments, data: studentAssignments }} empty="Заданий пока нет." />
            </Card>
            <Card title="Чеки на проверке" icon="receipt_long">
              {studentReceipts.map((receipt) => (
                <div className="row" key={receipt.id}>
                  <span>{money(receipt.amount, receipt.currency)} · {fmtDate(receipt.submitted_at)}</span>
                  <StatusPill status={receipt.status} />
                </div>
              ))}
              <ListState query={{ ...receipts, data: studentReceipts }} empty="Чеков на проверке нет." />
            </Card>
          </div>
        </div>
      </div>
    </AppShell>
  );
}

function StudentHeader({ student, summary }: { student: Async<StudentLink>; summary: Async<StudentSummary> }) {
  const name = student.data?.display_name ?? summary.data?.student_name ?? "Ученик";
  return (
    <div className="profile-header">
      <div className="avatar profile-avatar">{initials(name)}</div>
      <div className="profile-header-main">
        <div className="profile-title">
          <h2>{name}</h2>
          {student.data?.status && <StatusPill status={student.data.status} />}
        </div>
        <div className="profile-meta">
          {student.data?.subject && <span><Icon name="menu_book" />{student.data.subject}</span>}
          {student.data?.goal && <span><Icon name="flag" />{student.data.goal}</span>}
          {typeof student.data?.hourly_rate === "number" && <span><Icon name="payments" />{Math.round(student.data.hourly_rate)} ₽ / час</span>}
        </div>
      </div>
      <ErrorMsg error={student.error || summary.error} />
    </div>
  );
}

function Metric({ icon, label, value }: { icon: string; label: string; value: number | string }) {
  return (
    <div className="metric">
      <div className="label"><Icon name={icon} />{label}</div>
      <div className="value">{value}</div>
    </div>
  );
}

function CorrectionCard({
  studentId,
  balance,
  transactions,
  dashboard,
  summary,
}: {
  studentId: string;
  balance: Async<Balance>;
  transactions: Async<Transaction[]>;
  dashboard: Async<TeacherDashboard>;
  summary: Async<StudentSummary>;
}) {
  const [amount, setAmount] = useState("");
  const [comment, setComment] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const preview = useMemo(() => {
    const current = balance.data?.balance ?? 0;
    const delta = Number(amount || 0);
    return current + delta;
  }, [amount, balance.data?.balance]);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      await api.post(`/students/${studentId}/corrections`, {
        amount: Number(amount),
        comment,
      });
      setNotice("Коррекция применена.");
      setAmount("");
      setComment("");
      balance.reload();
      transactions.reload();
      dashboard.reload();
      summary.reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Корректировка баланса" icon="tune">
      <ErrorMsg error={error} />
      <Notice text={notice} />
      <div className="balance-preview">
        <div>
          <span className="hint">Сейчас</span>
          <strong>{money(balance.data?.balance, balance.data?.currency)}</strong>
        </div>
        <Icon name="arrow_forward" />
        <div>
          <span className="hint">Будет</span>
          <strong>{money(preview, balance.data?.currency)}</strong>
        </div>
      </div>
      <form onSubmit={submit}>
        <div className="field">
          <label>Сумма ± ₽<input type="number" value={amount} onChange={(event) => setAmount(event.target.value)} placeholder="например -500" required /></label>
        </div>
        <div className="field">
          <label>Комментарий<input value={comment} onChange={(event) => setComment(event.target.value)} placeholder="причина корректировки" required /></label>
        </div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Применение…" : "Применить"}</button>
      </form>
    </Card>
  );
}

function iconForTransaction(type: string): string {
  if (type === "payment") return "check_circle";
  if (type === "correction") return "tune";
  if (type === "refund") return "undo";
  return "school";
}

function labelForTransaction(type: string): string {
  if (type === "payment") return "Платёж";
  if (type === "correction") return "Коррекция";
  if (type === "refund") return "Возврат";
  return "Начисление";
}
