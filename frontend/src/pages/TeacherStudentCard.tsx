import { useMemo, useState } from "react";
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
import {
  AppShell,
  Avatar,
  Button,
  Card,
  EmptyState,
  ErrorMsg,
  Field,
  Icon,
  Modal,
  SkeletonRows,
  StatusPill,
  Textarea,
  fmtDate,
  useAsync,
  useToast,
} from "../ui";
import { useRealtimeEvent } from "../realtime";
import { money, signedMoney, teacherNav } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;
type StudentTab = "overview" | "lessons" | "assignments" | "finance" | "receipts";

const TABS: Array<[StudentTab, string]> = [
  ["overview", "Обзор"],
  ["finance", "Финансы"],
  ["lessons", "Занятия"],
  ["assignments", "Домашние задания"],
  ["receipts", "Чеки"],
];

// Влияние операции на долг: charge +, payment −, correction по знаку, refund +.
function txEffect(t: Transaction): number {
  if (t.type === "payment") return -Math.abs(t.amount);
  if (t.type === "correction") return t.amount;
  return Math.abs(t.amount);
}

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
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);

  useRealtimeEvent((event) => {
    if (["lesson", "assignment", "submission", "receipt", "balance"].some((t) => event.type.startsWith(t))) {
      balance.reload();
      transactions.reload();
      summary.reload();
    }
  }, [balance.reload, transactions.reload, summary.reload]);

  if (!studentId) return <Navigate to="/teacher/students" replace />;

  const displayName = student.data?.display_name ?? summary.data?.student_name ?? "Ученик";
  const studentLessons = (lessons.data ?? []).filter((l) => l.student_id === studentId);
  const studentAssignments = (assignments.data ?? []).filter((a) => a.student_id === studentId);
  const studentReceipts = (receipts.data ?? []).filter((r) => r.student_id === studentId);
  const currentBalance = balance.data?.balance ?? summary.data?.finance.balance_amount ?? 0;
  const currency = summary.data?.finance.currency ?? balance.data?.currency ?? "RUB";

  function reloadFinance() {
    balance.reload();
    transactions.reload();
    summary.reload();
    dashboard.reload();
  }

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
      actions={<Link className="button-like" to="/teacher/students"><Icon name="arrow_back" />Ученики</Link>}
    >
      <div className="container">
        <StudentHeader student={student} summary={summary} balance={currentBalance} currency={currency} />

        <div className="profile-tabs">
          {TABS.map(([value, label]) => (
            <button className={tab === value ? "active" : ""} key={value} onClick={() => setTab(value)}>
              {label}
            </button>
          ))}
        </div>

        {tab === "overview" && (
          <OverviewTab student={student} summary={summary} lessonsCount={studentLessons.length} assignments={studentAssignments} />
        )}

        {tab === "finance" && (
          <FinanceTab
            balance={currentBalance}
            currency={currency}
            transactions={transactions}
            summary={summary}
            onAdjust={() => setAdjustOpen(true)}
          />
        )}

        {tab === "lessons" && (
          <SectionTab title="Занятия ученика" icon="calendar_month" href="/teacher/lessons" loading={lessons.loading && !lessons.data}
            rows={studentLessons.map((l) => ({ id: l.id, title: l.topic || "Занятие", meta: `${fmtDate(l.starts_at)}`, status: l.status }))} />
        )}
        {tab === "assignments" && (
          <SectionTab title="Домашние задания ученика" icon="assignment" href="/teacher/assignments" loading={assignments.loading && !assignments.data}
            rows={studentAssignments.map((a) => ({ id: a.id, title: a.title, meta: a.due_at ? `дедлайн ${fmtDate(a.due_at)}` : "без срока", status: a.status, href: `/teacher/assignments/${a.id}/review` }))} />
        )}
        {tab === "receipts" && (
          <SectionTab title="Чеки ученика" icon="receipt_long" href="/teacher/receipts" loading={receipts.loading && !receipts.data}
            rows={studentReceipts.map((r) => ({ id: r.id, title: money(r.amount, r.currency), meta: fmtDate(r.submitted_at) || "—", status: r.status }))} />
        )}
      </div>

      {adjustOpen && (
        <AdjustModal
          studentId={studentId}
          studentName={displayName}
          currentBalance={currentBalance}
          currency={currency}
          onClose={() => setAdjustOpen(false)}
          onDone={reloadFinance}
        />
      )}
    </AppShell>
  );
}

function StudentHeader({
  student,
  summary,
  balance,
  currency,
}: {
  student: Async<StudentLink>;
  summary: Async<StudentSummary>;
  balance: number;
  currency: string;
}) {
  const name = student.data?.display_name ?? summary.data?.student_name ?? "Ученик";
  return (
    <div className="profile-header student-profile-header">
      <Avatar name={name} tone="teacher" size="lg" />
      <div className="profile-header-main">
        <div className="profile-title">
          <h2>{name}</h2>
          {student.data?.status && <StatusPill status={student.data.status} />}
        </div>
        <div className="profile-meta">
          {student.data?.subject && <span><Icon name="menu_book" />{student.data.subject}</span>}
          {student.data?.goal && <span><Icon name="flag" />{student.data.goal}</span>}
          {typeof student.data?.hourly_rate === "number" && <span><Icon name="payments" />{Math.round(student.data.hourly_rate)} ₽ / занятие</span>}
          <span><Icon name="account_balance_wallet" />{balance < 0 ? "переплата" : "долг"} {signedMoney(balance, currency)}</span>
        </div>
      </div>
      <Link className="button-like" to="/teacher/chat"><Icon name="chat_bubble" />Написать</Link>
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
  if (student.loading && !student.data) {
    return <Card title="Информация об ученике" icon="badge"><SkeletonRows count={3} /></Card>;
  }
  const activity = summary.data?.activity;
  return (
    <div className="student-overview-grid">
      <div className="dashboard-column">
        <Card title="Информация об ученике" icon="badge">
          <div className="student-info-grid">
            <InfoItem icon="menu_book" label="Предмет" value={student.data?.subject || "Не указан"} />
            <InfoItem icon="flag" label="Цель" value={student.data?.goal || "Не указана"} />
            <InfoItem icon="payments" label="Стоимость занятия" value={typeof student.data?.hourly_rate === "number" ? `${Math.round(student.data.hourly_rate)} ₽` : "Не указана"} />
            <InfoItem icon="check_circle" label="Статус" value={student.data?.status || "—"} />
            <InfoItem icon="mail" label="Email" value="нет в текущем API" />
            <InfoItem icon="event" label="Обновлено" value={summary.data?.updated_at ? fmtDate(summary.data.updated_at) : "—"} />
          </div>
        </Card>

        <Card title="Заметки и доп. информация" icon="sticky_note_2">
          <EmptyState
            icon="lock"
            title="Редактирование пока недоступно"
            hint="Заметки, метки и доп. поля профиля появятся после backend-эндпоинта обновления ученика (PATCH /students/{id})."
          />
        </Card>
      </div>

      <div className="dashboard-column">
        <Card title="Активность" icon="monitoring">
          <div className="activity-grid">
            <StatBox value={activity?.completed_lessons_count ?? lessonsCount} label="занятий проведено" />
            <StatBox value={activity?.reviewed_assignments_count ?? 0} label="ДЗ выполнено" success />
            <StatBox value={activity?.upcoming_lessons_count ?? 0} label="ближайшие занятия" />
            <StatBox value={assignments.filter((a) => a.status !== "reviewed" && a.status !== "accepted" && a.status !== "done").length} label="активных ДЗ" />
          </div>
        </Card>
      </div>
    </div>
  );
}

function FinanceTab({
  balance,
  currency,
  transactions,
  summary,
  onAdjust,
}: {
  balance: number;
  currency: string;
  transactions: Async<Transaction[]>;
  summary: Async<StudentSummary>;
  onAdjust: () => void;
}) {
  const list = transactions.data ?? [];
  const accrued = list.filter((t) => t.type === "charge").reduce((s, t) => s + Math.abs(t.amount), 0);
  const paid = list.filter((t) => t.type === "payment").reduce((s, t) => s + Math.abs(t.amount), 0);
  const pendingAmount = summary.data?.finance.pending_receipts_amount ?? 0;
  const pendingCount = summary.data?.finance.pending_receipts_count ?? 0;
  const isCredit = balance < 0;
  const isZero = balance === 0;

  // Журнал с бегущим балансом: считаем по возрастанию даты, показываем сверху новые.
  const withRunning = useMemo(() => {
    const sorted = [...list].sort((a, b) => new Date(a.created_at ?? 0).getTime() - new Date(b.created_at ?? 0).getTime());
    let run = 0;
    const mapped = sorted.map((t) => {
      run += txEffect(t);
      return { tx: t, running: run };
    });
    return mapped.reverse();
  }, [list]);

  return (
    <>
      <div className="student-finance-row">
        <div className={"student-balance-card" + (isCredit ? " is-credit" : "")}>
          <div className="label"><Icon name="account_balance_wallet" />{isCredit ? "Переплата (в пользу ученика)" : isZero ? "Баланс закрыт" : "Текущий долг"}</div>
          <div className="value">{signedMoney(balance, currency)}</div>
          <div className="hint">{isCredit ? "можно зачесть в счёт занятий" : isZero ? "оплачено полностью" : "к оплате преподавателю"}</div>
          <Button variant="secondary" icon="tune" onClick={onAdjust}>Скорректировать баланс</Button>
        </div>
        <MetricCard icon="trending_up" label="Начислено за занятия" value={money(accrued, currency)} hint="по журналу операций" />
        <MetricCard icon="check_circle" label="Оплачено (подтверждено)" value={money(paid, currency)} hint="только подтверждённые чеки" success />
      </div>

      {pendingAmount > 0 && (
        <div className="rule-banner student-pending-note">
          <Icon name="hourglass_top" />
          <span>{pendingCount} чек(ов) на {money(pendingAmount, currency)} ожидают подтверждения. Долг не уменьшится, пока вы не подтвердите оплату.</span>
          <Link className="button-like secondary small" to="/teacher/receipts">Открыть чеки</Link>
        </div>
      )}

      <Card title="Журнал операций" icon="receipt_long">
        {transactions.loading && !transactions.data ? (
          <SkeletonRows count={4} />
        ) : transactions.error ? (
          <ErrorMsg error={transactions.error} />
        ) : list.length === 0 ? (
          <EmptyState icon="receipt_long" title="Операций пока нет" hint="Здесь появятся начисления, оплаты и корректировки." />
        ) : (
          <>
            <div className="journal-head">
              <span></span><span>Операция</span><span>Сумма</span><span>Баланс</span>
            </div>
            {withRunning.map(({ tx, running }) => {
              const effect = txEffect(tx);
              return (
                <div className="journal-row" key={tx.id}>
                  <div className="transaction-icon"><Icon name={iconForTransaction(tx.type)} /></div>
                  <div>
                    <strong>{labelForTransaction(tx.type)}</strong>
                    <span>{tx.comment || fmtDate(tx.created_at) || "—"}</span>
                  </div>
                  <strong className={effect < 0 ? "amount-negative" : "amount-positive"}>
                    {effect < 0 ? "−" : "+"}{money(Math.abs(effect), tx.currency)}
                  </strong>
                  <span className={running < 0 ? "finance-credit" : ""}>{signedMoney(running, tx.currency)}</span>
                </div>
              );
            })}
          </>
        )}
      </Card>
    </>
  );
}

interface SectionRow { id: string; title: string; meta: string; status: string; href?: string }

function SectionTab({ title, icon, href, rows, loading }: { title: string; icon: string; href: string; rows: SectionRow[]; loading: boolean }) {
  return (
    <Card title={title} icon={icon} actions={<Link className="card-head-link" to={href}>Открыть раздел</Link>}>
      {loading ? (
        <SkeletonRows count={3} />
      ) : rows.length === 0 ? (
        <EmptyState icon={icon} title="Пока пусто" hint="Здесь появятся записи этого ученика." />
      ) : (
        rows.map((row) => (
          <div className="dash-row" key={row.id}>
            <div className="dash-main">
              <div className="t">{row.href ? <Link to={row.href}>{row.title}</Link> : row.title}</div>
              <div className="s">{row.meta}</div>
            </div>
            <StatusPill status={row.status} />
          </div>
        ))
      )}
    </Card>
  );
}

function AdjustModal({
  studentId,
  studentName,
  currentBalance,
  currency,
  onClose,
  onDone,
}: {
  studentId: string;
  studentName: string;
  currentBalance: number;
  currency: string;
  onClose: () => void;
  onDone: () => void;
}) {
  const toast = useToast();
  const [mode, setMode] = useState<"charge" | "credit">("charge");
  const [amount, setAmount] = useState("");
  const [comment, setComment] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const numeric = Number(amount || 0);
  const signed = (mode === "charge" ? 1 : -1) * numeric;
  const preview = currentBalance + signed;
  const ready = numeric > 0 && comment.trim().length > 0;

  async function submit() {
    if (numeric === 0) {
      toast({ tone: "warning", title: "Нулевая корректировка не отправлена", body: "Укажите сумму больше нуля." });
      return;
    }
    if (!comment.trim()) {
      setError("Добавьте комментарий к корректировке.");
      return;
    }
    setError(null);
    setBusy(true);
    try {
      await api.post(`/students/${studentId}/corrections`, { amount: signed, currency, comment: comment.trim() });
      toast({ tone: "success", title: "Баланс скорректирован", body: `${signed < 0 ? "−" : "+"}${Math.abs(signed)} ${currency} · ${comment.trim()}` });
      onDone();
      onClose();
    } catch (err) {
      setError((err as Error).message);
      toast({ tone: "danger", title: "Не удалось сохранить", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      title="Ручная корректировка"
      subtitle={`${studentName} · текущий ${currentBalance < 0 ? "переплата" : "долг"} ${signedMoney(currentBalance, currency)}`}
      onClose={onClose}
      onSubmit={submit}
      footer={
        <>
          <Button type="button" onClick={onClose}>Отмена</Button>
          <Button variant="primary" type="submit" icon="check" loading={busy} disabled={!ready}>Применить</Button>
        </>
      }
    >
      <ErrorMsg error={error} />
      <div className="direction-toggle">
        <button type="button" className={mode === "charge" ? "active" : ""} onClick={() => setMode("charge")}><Icon name="add" />Начислить долг</button>
        <button type="button" className={mode === "credit" ? "active" : ""} onClick={() => setMode("credit")}><Icon name="remove" />Списать / зачесть</button>
      </div>
      <div className="modal-fields">
        <Field label="Сумма">
          <span className="amount-input">
            <input type="number" min="1" value={amount} onChange={(e) => setAmount(e.target.value)} placeholder="0" autoFocus />
            <span>₽</span>
          </span>
        </Field>
        <Field label="Комментарий (обязательно)">
          <Textarea value={comment} onChange={(e) => setComment(e.target.value)} placeholder="Например: зачёт за вводное занятие, скидка, бонус…" />
        </Field>
      </div>
      <div className="balance-preview">
        <div>
          <span className="hint">Сейчас</span>
          <strong>{signedMoney(currentBalance, currency)}</strong>
        </div>
        <Icon name="arrow_forward" />
        <div>
          <span className="hint">Станет</span>
          <strong className={preview < 0 ? "amount-negative" : preview > 0 ? "amount-positive" : ""}>{signedMoney(preview, currency)}</strong>
        </div>
      </div>
    </Modal>
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
  if (type === "payment") return "Платёж подтверждён";
  if (type === "correction") return "Корректировка";
  if (type === "refund") return "Возврат";
  return "Начисление за занятие";
}
