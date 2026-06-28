import { useState, type FormEvent } from "react";
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
import { AppShell, Card, ErrorMsg, Icon, ListState, StatusPill, fmtDate, useAsync } from "../ui";
import { initials, money, teacherNav } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;
type StudentTab = "overview" | "lessons" | "assignments" | "finance" | "receipts";

const TABS: Array<[StudentTab, string]> = [
  ["overview", "Профиль"],
  ["lessons", "Занятия"],
  ["assignments", "Домашние задания"],
  ["finance", "Финансы"],
  ["receipts", "Чеки"],
];

export default function TeacherStudentCard() {
  const { studentId } = useParams();
  const [tab, setTab] = useState<StudentTab>("overview");
  const [adjustOpen, setAdjustOpen] = useState(false);
  const dashboard = useAsync<TeacherDashboard>(() => api.get("/dashboard/teacher"), []);
  const student = useAsync<StudentLink>(() => api.get(`/students/${studentId}`), [studentId]);
  const summary = useAsync<StudentSummary>(() => api.get(`/students/${studentId}/summary`), [studentId]);
  const balance = useAsync<Balance>(() => api.get(`/students/${studentId}/balance`), [studentId]);
  const transactions = useAsync<Transaction[]>(() => api.get(`/students/${studentId}/transactions`), [studentId]);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts?status=pending_review"), []);

  if (!studentId) return <Navigate to="/teacher/students" replace />;

  const displayName = student.data?.display_name ?? summary.data?.student_name ?? "Ученик";
  const studentLessons = (lessons.data ?? []).filter((lesson) => lesson.student_id === studentId);
  const studentAssignments = (assignments.data ?? []).filter((assignment) => assignment.student_id === studentId);
  const studentReceipts = (receipts.data ?? []).filter((receipt) => receipt.student_id === studentId);
  const debt = summary.data?.finance.debt_amount ?? Math.max(0, balance.data?.balance ?? 0);
  const overpaid = summary.data?.finance.overpaid_amount ?? Math.max(0, -(balance.data?.balance ?? 0));
  const currency = summary.data?.finance.currency ?? balance.data?.currency ?? "RUB";

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
      actions={<Link className="button-link" to="/teacher/students"><Icon name="arrow_back" />Ученики</Link>}
    >
      <div className="container">
        <StudentHeader student={student} summary={summary} />

        <div className="profile-tabs">
          {TABS.map(([value, label]) => (
            <button className={tab === value ? "active" : ""} key={value} onClick={() => setTab(value)}>
              {label}
            </button>
          ))}
        </div>

        {tab === "overview" && (
          <OverviewTab
            student={student}
            summary={summary}
            lessonsCount={studentLessons.length}
            assignments={studentAssignments}
          />
        )}

        {tab === "finance" && (
          <FinanceTab
            debt={debt}
            overpaid={overpaid}
            currency={currency}
            transactions={transactions}
            summary={summary}
            receipts={studentReceipts}
            onAdjust={() => setAdjustOpen(true)}
          />
        )}

        {tab !== "overview" && tab !== "finance" && (
          <PlaceholderTab
            tab={tab}
            lessons={studentLessons}
            assignments={studentAssignments}
            receipts={studentReceipts}
            loading={lessons.loading || assignments.loading || receipts.loading}
          />
        )}
      </div>

      {adjustOpen && (
        <AdjustModal
          studentId={studentId}
          studentName={displayName}
          balance={balance}
          transactions={transactions}
          dashboard={dashboard}
          summary={summary}
          onClose={() => setAdjustOpen(false)}
        />
      )}
    </AppShell>
  );
}

function StudentHeader({ student, summary }: { student: Async<StudentLink>; summary: Async<StudentSummary> }) {
  const name = student.data?.display_name ?? summary.data?.student_name ?? "Ученик";
  return (
    <div className="profile-header student-profile-header">
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
      <Link className="button-link" to="/teacher/chat"><Icon name="chat_bubble" />Написать</Link>
      <ErrorMsg error={student.error || summary.error} />
    </div>
  );
}

function OverviewTab({
  student,
  summary,
  lessonsCount,
  assignments,
}: {
  student: Async<StudentLink>;
  summary: Async<StudentSummary>;
  lessonsCount: number;
  assignments: Assignment[];
}) {
  return (
    <div className="student-overview-grid">
      <div className="dashboard-column">
        <Card title="Информация об ученике" icon="badge">
          <div className="student-info-grid">
            <InfoItem icon="mail" label="Email" value="Недоступно в текущем API" />
            <InfoItem icon="menu_book" label="Предмет" value={student.data?.subject || "Не указан"} />
            <InfoItem icon="flag" label="Цель" value={student.data?.goal || "Не указана"} />
            <InfoItem icon="payments" label="Стоимость занятия" value={typeof student.data?.hourly_rate === "number" ? `${Math.round(student.data.hourly_rate)} ₽ / час` : "Не указана"} />
            <InfoItem icon="event" label="Занимается" value={summary.data?.updated_at ? `обновлено ${fmtDate(summary.data.updated_at)}` : "Дата не указана"} />
            <InfoItem icon="check_circle" label="Статус" value={student.data?.status || "—"} />
          </div>
        </Card>

        <Card title="Заметки и доп. информация" icon="sticky_note_2">
          <textarea className="notes-textarea" placeholder="Личные заметки: цели, сильные и слабые темы, договоренности..." />
          <div className="extra-fields-placeholder">
            <span><Icon name="label" />Родитель</span>
            <strong>Добавьте контакт после появления поля в контракте</strong>
          </div>
        </Card>
      </div>

      <div className="dashboard-column">
        <Card title="Активность" icon="monitoring">
          <div className="activity-grid">
            <StatBox value={summary.data?.activity.completed_lessons_count ?? lessonsCount} label="занятий проведено" />
            <StatBox value={summary.data?.activity.reviewed_assignments_count ?? 0} label="ДЗ выполнено" success />
            <StatBox value={summary.data?.activity.upcoming_lessons_count ?? 0} label="ближайшие" />
            <StatBox value={assignments.filter((item) => item.status !== "reviewed" && item.status !== "accepted").length} label="активных ДЗ" />
          </div>
        </Card>
        <Card title="Метки" icon="sell">
          <div className="tag-list">
            <span>активный ученик</span>
            {student.data?.subject && <span>{student.data.subject}</span>}
            {student.data?.goal && <span>{student.data.goal}</span>}
          </div>
        </Card>
      </div>
    </div>
  );
}

function FinanceTab({
  debt,
  overpaid,
  currency,
  transactions,
  summary,
  receipts,
  onAdjust,
}: {
  debt: number;
  overpaid: number;
  currency: string;
  transactions: Async<Transaction[]>;
  summary: Async<StudentSummary>;
  receipts: Receipt[];
  onAdjust: () => void;
}) {
  const accrued = (transactions.data ?? []).filter((transaction) => transaction.type === "charge").reduce((sum, transaction) => sum + Math.abs(transaction.amount), 0);
  const paid = (transactions.data ?? []).filter((transaction) => transaction.type === "payment").reduce((sum, transaction) => sum + Math.abs(transaction.amount), 0);
  const pendingAmount = summary.data?.finance.pending_receipts_amount ?? receipts.reduce((sum, receipt) => sum + receipt.amount, 0);

  return (
    <>
      <div className="student-finance-row">
        <div className="student-balance-card">
          <div className="label"><Icon name="account_balance_wallet" />{debt > 0 ? "Текущий долг" : overpaid > 0 ? "Переплата" : "Баланс закрыт"}</div>
          <div className="value">{money(debt > 0 ? debt : overpaid, currency)}</div>
          <div className="hint">{debt > 0 ? "ожидает оплаты" : overpaid > 0 ? "учтено авансом" : "долга нет"}</div>
          <button type="button" onClick={onAdjust}><Icon name="tune" />Скорректировать баланс</button>
        </div>
        <MetricCard icon="trending_up" label="Начислено за занятия" value={money(accrued, currency)} hint="по журналу операций" />
        <MetricCard icon="check_circle" label="Оплачено подтверждено" value={money(paid, currency)} hint="только подтвержденные чеки" success />
      </div>

      {pendingAmount > 0 && (
        <div className="rule-banner student-pending-note">
          <Icon name="hourglass_top" />
          <span>Чеки на {money(pendingAmount, currency)} ожидают подтверждения. Долг не уменьшится, пока вы не подтвердите оплату.</span>
          <Link className="button-link small-link" to="/teacher/receipts">Открыть чеки</Link>
        </div>
      )}

      <Card title="Журнал операций" icon="receipt_long">
        <div className="journal-head">
          <span></span><span>Операция</span><span>Сумма</span><span>Дата</span>
        </div>
        {(transactions.data ?? []).map((transaction) => (
          <div className="journal-row" key={transaction.id}>
            <div className="transaction-icon"><Icon name={iconForTransaction(transaction.type)} /></div>
            <div>
              <strong>{labelForTransaction(transaction.type)}</strong>
              <span>{transaction.comment || transaction.lesson_id || transaction.receipt_id || "—"}</span>
            </div>
            <strong className={transaction.type === "payment" || transaction.amount < 0 ? "amount-negative" : ""}>
              {transaction.amount > 0 ? "+" : "−"}{money(Math.abs(transaction.amount), transaction.currency)}
            </strong>
            <span className="muted">{fmtDate(transaction.created_at) || "—"}</span>
          </div>
        ))}
        <ListState query={transactions} empty="Операций пока нет." />
      </Card>
    </>
  );
}

function PlaceholderTab({
  tab,
  lessons,
  assignments,
  receipts,
  loading,
}: {
  tab: Exclude<StudentTab, "overview" | "finance">;
  lessons: Lesson[];
  assignments: Assignment[];
  receipts: Receipt[];
  loading: boolean;
}) {
  if (tab === "lessons") {
    return <LinkedList title="Занятия ученика" icon="calendar_month" href="/teacher/lessons" rows={lessons.map((lesson) => `${lesson.topic || "Занятие"} · ${fmtDate(lesson.starts_at)} · ${lesson.status}`)} loading={loading} />;
  }
  if (tab === "assignments") {
    return <LinkedList title="Домашние задания ученика" icon="assignment" href="/teacher/assignments" rows={assignments.map((assignment) => `${assignment.title} · ${assignment.status}`)} loading={loading} />;
  }
  return <LinkedList title="Чеки ученика" icon="receipt_long" href="/teacher/receipts" rows={receipts.map((receipt) => `${money(receipt.amount, receipt.currency)} · ${fmtDate(receipt.submitted_at)} · ${receipt.status}`)} loading={loading} />;
}

function LinkedList({ title, icon, href, rows, loading }: { title: string; icon: string; href: string; rows: string[]; loading: boolean }) {
  return (
    <Card title={title} icon={icon} actions={<Link className="button-link small-link" to={href}>Открыть раздел</Link>}>
      {loading && rows.length === 0 && <p className="hint">Загрузка...</p>}
      {rows.map((row) => <div className="row" key={row}><span>{row}</span></div>)}
      {!loading && rows.length === 0 && <p className="hint">В этом разделе пока пусто.</p>}
    </Card>
  );
}

function AdjustModal({
  studentId,
  studentName,
  balance,
  transactions,
  dashboard,
  summary,
  onClose,
}: {
  studentId: string;
  studentName: string;
  balance: Async<Balance>;
  transactions: Async<Transaction[]>;
  dashboard: Async<TeacherDashboard>;
  summary: Async<StudentSummary>;
  onClose: () => void;
}) {
  const [mode, setMode] = useState<"charge" | "credit">("charge");
  const [amount, setAmount] = useState("");
  const [comment, setComment] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const current = balance.data?.balance ?? 0;
  const signedAmount = (mode === "charge" ? 1 : -1) * Number(amount || 0);
  const preview = current + signedAmount;
  const ready = Number(amount) > 0 && comment.trim().length > 0;

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (!ready) return;
    setError(null);
    setBusy(true);
    try {
      await api.post(`/students/${studentId}/corrections`, {
        amount: signedAmount,
        comment,
      });
      balance.reload();
      transactions.reload();
      dashboard.reload();
      summary.reload();
      onClose();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="modal-overlay" onMouseDown={onClose}>
      <form className="modal-panel" onMouseDown={(event) => event.stopPropagation()} onSubmit={submit}>
        <div className="modal-heading">
          <div>
            <h2>Ручная корректировка</h2>
            <p>{studentName} · текущий баланс {money(current, balance.data?.currency)}</p>
          </div>
          <button className="icon-button" type="button" onClick={onClose} title="Закрыть"><Icon name="close" /></button>
        </div>
        <ErrorMsg error={error} />
        <div className="direction-toggle">
          <button type="button" className={mode === "charge" ? "active" : ""} onClick={() => setMode("charge")}><Icon name="add" />Начислить долг</button>
          <button type="button" className={mode === "credit" ? "active" : ""} onClick={() => setMode("credit")}><Icon name="remove" />Списать / зачесть</button>
        </div>
        <div className="modal-fields">
          <div className="field">
            <label>Сумма
              <span className="amount-input">
                <input type="number" min="1" value={amount} onChange={(event) => setAmount(event.target.value)} placeholder="0" required />
                <span>₽</span>
              </span>
            </label>
          </div>
          <div className="field"><label>Комментарий <textarea value={comment} onChange={(event) => setComment(event.target.value)} required placeholder="Например: скидка, бонус, корректировка..." /></label></div>
        </div>
        <div className="balance-preview">
          <div><span className="hint">Сейчас</span><strong>{money(current, balance.data?.currency)}</strong></div>
          <Icon name="arrow_forward" />
          <div><span className="hint">Станет</span><strong>{money(preview, balance.data?.currency)}</strong></div>
        </div>
        <div className="modal-actions">
          <button type="button" onClick={onClose}>Отмена</button>
          <button className="primary" type="submit" disabled={busy || !ready}><Icon name="check" />{busy ? "Применение..." : "Применить"}</button>
        </div>
      </form>
    </div>
  );
}

function InfoItem({ icon, label, value }: { icon: string; label: string; value: string }) {
  return <div className="info-item"><Icon name={icon} /><div><span>{label}</span><strong>{value}</strong></div></div>;
}

function StatBox({ value, label, success }: { value: number | string; label: string; success?: boolean }) {
  return <div className="stat-box"><strong className={success ? "success" : ""}>{value}</strong><span>{label}</span></div>;
}

function MetricCard({ icon, label, value, hint, success }: { icon: string; label: string; value: string; hint: string; success?: boolean }) {
  return (
    <div className="metric">
      <div className="label"><Icon name={icon} />{label}</div>
      <div className={"value" + (success ? " metric-success" : "")}>{value}</div>
      <div className="hint">{hint}</div>
    </div>
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
