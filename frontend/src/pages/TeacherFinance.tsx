import { useCallback, useEffect, useMemo, useState } from "react";
import { Link } from "react-router-dom";
import {
  api,
  reports,
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
  ErrorState,
  Field,
  Icon,
  Input,
  Metric,
  Modal,
  Select,
  SkeletonRows,
  StatusPill,
  Textarea,
  useAsync,
  useToast,
} from "../ui";
import { money as formatMoney, signedMoney as formatSignedBalance, teacherNav } from "./teacherNav";

type FinanceFilter = "all" | "debt" | "pending" | "overpaid";
type TxFilter = "all" | "charge" | "payment" | "correction" | "refund";

function shortDate(iso?: string): string {
  if (!iso) return "-";
  const date = new Date(iso);
  if (isNaN(date.getTime())) return "-";
  return date.toLocaleString("ru-RU", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
}

function studentName(students: StudentLink[], summary: StudentSummary): string {
  return summary.student_name || students.find((student) => student.student_id === summary.student_id)?.display_name || summary.student_id.slice(0, 8);
}

function studentSubject(students: StudentLink[], studentId: string): string {
  const student = students.find((item) => item.student_id === studentId);
  return student?.subject || student?.goal || "Предмет не указан";
}

function financeTag(summary: StudentSummary) {
  if (summary.finance.pending_receipts_count > 0) return { label: "чек на проверке", className: "finance-tag-warning" };
  if (summary.finance.balance_amount < 0 || summary.finance.overpaid_amount > 0) return { label: "переплата", className: "finance-tag-success" };
  if (summary.finance.balance_amount === 0 || summary.finance.debt_amount <= 0) return { label: "оплачено", className: "finance-tag-muted" };
  return { label: "есть долг", className: "finance-tag-warning" };
}

function transactionLabel(type: string): string {
  if (type === "payment") return "Платёж подтверждён";
  if (type === "correction") return "Корректировка";
  if (type === "refund") return "Возврат";
  return "Начисление";
}

function transactionIcon(type: string): string {
  if (type === "payment") return "check_circle";
  if (type === "correction") return "tune";
  if (type === "refund") return "undo";
  return "school";
}

function transactionEffect(transaction: Transaction): number {
  if (transaction.type === "payment") return -Math.abs(transaction.amount);
  if (transaction.type === "correction") return transaction.amount;
  return Math.abs(transaction.amount);
}

export default function TeacherFinance() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const toast = useToast();
  const [filter, setFilter] = useState<FinanceFilter>("all");
  const [txFilter, setTxFilter] = useState<TxFilter>("all");
  const [studentId, setStudentId] = useState("");
  const [transactions, setTransactions] = useState<Transaction[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [loadingTx, setLoadingTx] = useState(false);
  const [correctionOpen, setCorrectionOpen] = useState(false);

  const studentLinks = students.data ?? [];
  const summaries = dashboard.data?.students ?? [];

  const loadTransactions = useCallback((id: string) => {
    if (!id) {
      setTransactions([]);
      return;
    }
    setLoadingTx(true);
    setError(null);
    api
      .get<Transaction[]>(`/students/${id}/transactions`)
      .then((items) => setTransactions(items.sort((a, b) => new Date(b.created_at ?? 0).getTime() - new Date(a.created_at ?? 0).getTime())))
      .catch((err) => setError((err as Error).message))
      .finally(() => setLoadingTx(false));
  }, []);

  useEffect(() => {
    if (!studentId && summaries.length > 0) setStudentId(summaries[0].student_id);
  }, [studentId, summaries]);

  useEffect(() => {
    loadTransactions(studentId);
  }, [loadTransactions, studentId]);

  const visibleSummaries = useMemo(() => summaries.filter((summary) => {
    if (filter === "debt") return summary.finance.debt_amount > 0 || summary.finance.balance_amount > 0;
    if (filter === "pending") return summary.finance.pending_receipts_count > 0;
    if (filter === "overpaid") return summary.finance.overpaid_amount > 0 || summary.finance.balance_amount < 0;
    return true;
  }), [filter, summaries]);

  const selectedSummary = summaries.find((summary) => summary.student_id === studentId);
  const filteredTransactions = useMemo(
    () => transactions.filter((transaction) => txFilter === "all" || transaction.type === txFilter),
    [transactions, txFilter],
  );

  const totalPaid = transactions.filter((tx) => tx.type === "payment").reduce((sum, tx) => sum + Math.abs(tx.amount), 0);
  const currency = selectedSummary?.finance.currency ?? dashboard.data?.students[0]?.finance.currency ?? "RUB";

  function afterCorrection() {
    dashboard.reload();
    loadTransactions(studentId);
  }

  return (
    <AppShell
      title="Финансы"
      subtitle="Сводка по всем ученикам"
      navSection="Работа"
      navItems={teacherNav("finance", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={
        <div className="btn-group">
          <Button variant="secondary" icon="tune" disabled={!selectedSummary} onClick={() => setCorrectionOpen(true)}>Корректировка</Button>
          <Link className="button-like primary has-icon" to="/teacher/receipts"><Icon name="receipt_long" />Чеки</Link>
        </div>
      }
    >
      <div className="container">
        <div className="finance-kpi-grid">
          <div className="finance-hero-card">
            <div className="label"><Icon name="account_balance_wallet" />Суммарный долг</div>
            <div className="value">{formatMoney(dashboard.data?.total_debt_amount, currency)}</div>
            <div className="hint">{dashboard.data?.students_with_debt_count ?? 0} должников · переплаты {formatMoney(dashboard.data?.total_overpaid_amount, currency)}</div>
          </div>
          <Metric icon="groups" label="Ученики" value={dashboard.data?.students_count ?? 0} sub="с финансовой сводкой" />
          <Metric icon="check_circle" label="Оплачено у выбранного" value={formatMoney(totalPaid, currency)} sub={selectedSummary ? studentName(studentLinks, selectedSummary) : "выберите ученика"} subTone="pos" />
          <Metric icon="hourglass_top" label="На проверке" value={formatMoney(dashboard.data?.pending_receipts_amount, currency)} sub={`${dashboard.data?.pending_receipts_count ?? 0} чеков · не уменьшают долг`} tone="warn" />
        </div>

        <div className="dashboard-grid">
          <Card
            title="Долги по ученикам"
            icon="groups"
            actions={
              <div className="inline-filter">
                {[
                  ["all", "Все"],
                  ["debt", "С долгом"],
                  ["pending", "Ждут чек"],
                  ["overpaid", "Переплаты"],
                ].map(([value, label]) => (
                  <button className={filter === value ? "active" : ""} key={value} onClick={() => setFilter(value as FinanceFilter)}>
                    {label}
                  </button>
                ))}
              </div>
            }
          >
            <div className="finance-table-head">
              <span>Ученик</span>
              <span>Баланс</span>
              <span>Статус оплат</span>
            </div>
            {dashboard.loading && !dashboard.data ? (
              <SkeletonRows count={5} />
            ) : dashboard.error ? (
              <ErrorState error={dashboard.error} onRetry={dashboard.reload} />
            ) : visibleSummaries.length === 0 ? (
              <EmptyState icon="account_balance_wallet" title="По фильтру пусто" hint="Финансовых записей в этой категории нет." />
            ) : (
              visibleSummaries.map((summary) => {
                const name = studentName(studentLinks, summary);
                const tag = financeTag(summary);
                const balance = summary.finance.balance_amount;
                return (
                  <button
                    className={"finance-student-row" + (studentId === summary.student_id ? " active" : "")}
                    key={summary.student_id}
                    onClick={() => setStudentId(summary.student_id)}
                    type="button"
                  >
                    <span className="finance-student-main">
                      <Avatar name={name} tone={balance < 0 ? "student" : balance > 0 ? "teacher" : "muted"} size="sm" />
                      <span>
                        <strong>{name}</strong>
                        <em>{studentSubject(studentLinks, summary.student_id)}</em>
                      </span>
                    </span>
                    <strong className={balance > 0 ? "finance-debt" : balance < 0 ? "finance-credit" : "finance-zero"}>
                      {formatSignedBalance(balance, summary.finance.currency)}
                    </strong>
                    <span className={"finance-tag " + tag.className}>{tag.label}</span>
                  </button>
                );
              })
            )}
          </Card>

          <div className="dashboard-column">
            <Card
              title="Журнал операций"
              icon="history"
              actions={
                <Select value={txFilter} onChange={(event) => setTxFilter(event.target.value as TxFilter)}>
                  <option value="all">Все типы</option>
                  <option value="charge">Начисления</option>
                  <option value="payment">Оплаты</option>
                  <option value="correction">Коррекции</option>
                  <option value="refund">Возвраты</option>
                </Select>
              }
            >
              <div className="card-tools">
                <Select value={studentId} onChange={(event) => setStudentId(event.target.value)}>
                  <option value="">Выберите ученика</option>
                  {summaries.map((summary) => (
                    <option key={summary.student_id} value={summary.student_id}>{studentName(studentLinks, summary)}</option>
                  ))}
                </Select>
              </div>
              {error && !loadingTx && <ErrorState error={error} onRetry={() => loadTransactions(studentId)} />}
              {loadingTx && <SkeletonRows count={3} />}
              {!loadingTx && filteredTransactions.map((transaction) => {
                const effect = transactionEffect(transaction);
                return (
                  <div className="finance-operation-row" key={transaction.id}>
                    <div className="transaction-icon"><Icon name={transactionIcon(transaction.type)} /></div>
                    <div className="finance-operation-main">
                      <div className="summary-title">
                        {transactionLabel(transaction.type)}
                        <StatusPill status={transaction.type} />
                      </div>
                      <div className="muted">{transaction.comment || shortDate(transaction.created_at)}</div>
                    </div>
                    <strong className={effect < 0 ? "amount-negative" : "amount-positive"}>
                      {effect < 0 ? "-" : "+"}{formatMoney(Math.abs(effect), transaction.currency)}
                    </strong>
                  </div>
                );
              })}
              {!loadingTx && !error && filteredTransactions.length === 0 && (
                <EmptyState icon="history" title={studentId ? "Операций нет" : "Выберите ученика"} hint="Здесь появятся начисления, оплаты и корректировки." />
              )}
            </Card>

            <div className="pending-receipts-card">
              <div>
                <Icon name="hourglass_top" />
                <strong>{dashboard.data?.pending_receipts_count ?? 0} чеков на проверке</strong>
              </div>
              <p>На сумму {formatMoney(dashboard.data?.pending_receipts_amount, currency)}. Pending-чек не уменьшает долг до подтверждения преподавателем.</p>
              <Link className="primary-action" to="/teacher/receipts"><Icon name="receipt_long" />Перейти к чекам</Link>
            </div>
          </div>
        </div>
      </div>

      {correctionOpen && selectedSummary && (
        <CorrectionModal
          summary={selectedSummary}
          studentName={studentName(studentLinks, selectedSummary)}
          onClose={() => setCorrectionOpen(false)}
          onDone={() => {
            setCorrectionOpen(false);
            toast({ tone: "success", title: "Корректировка добавлена", body: "Баланс обновится после обработки события." });
            afterCorrection();
          }}
        />
      )}
    </AppShell>
  );
}

function CorrectionModal({
  summary,
  studentName,
  onClose,
  onDone,
}: {
  summary: StudentSummary;
  studentName: string;
  onClose: () => void;
  onDone: () => void;
}) {
  const toast = useToast();
  const [amount, setAmount] = useState("");
  const [comment, setComment] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const numericAmount = Number(amount);

  async function submit() {
    setError(null);
    if (!Number.isFinite(numericAmount) || numericAmount === 0) {
      toast({ tone: "warning", title: "Нулевая корректировка не отправлена", body: "Укажите положительную или отрицательную сумму." });
      return;
    }
    if (!comment.trim()) {
      setError("Добавьте комментарий к корректировке.");
      return;
    }
    setBusy(true);
    try {
      await api.post(`/students/${summary.student_id}/corrections`, {
        amount: numericAmount,
        currency: summary.finance.currency,
        comment: comment.trim(),
      });
      onDone();
    } catch (err) {
      setError((err as Error).message);
      toast({ tone: "danger", title: "Корректировка не сохранена", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  const nextBalance = (summary.finance.balance_amount ?? 0) + (Number.isFinite(numericAmount) ? numericAmount : 0);

  return (
    <Modal
      title="Корректировка баланса"
      subtitle={`${studentName} · текущий баланс ${formatSignedBalance(summary.finance.balance_amount, summary.finance.currency)}`}
      onClose={onClose}
      onSubmit={submit}
      footer={
        <>
          <Button variant="secondary" onClick={onClose}>Отмена</Button>
          <Button variant="primary" icon="check" loading={busy} type="submit">Добавить</Button>
        </>
      }
    >
      <ErrorMsg error={error} />
      <div className="balance-preview">
        <div>
          <span className="hint">Сейчас</span>
          <strong>{formatSignedBalance(summary.finance.balance_amount, summary.finance.currency)}</strong>
        </div>
        <Icon name="arrow_forward" />
        <div>
          <span className="hint">После корректировки</span>
          <strong className={nextBalance < 0 ? "amount-negative" : nextBalance > 0 ? "amount-positive" : ""}>
            {formatSignedBalance(nextBalance, summary.finance.currency)}
          </strong>
        </div>
      </div>
      <Field label="Сумма со знаком" hint="Плюс увеличивает долг, минус уменьшает долг или создаёт переплату.">
        <Input type="number" step="1" value={amount} onChange={(event) => setAmount(event.target.value)} placeholder="-500" autoFocus />
      </Field>
      <Field label="Комментарий" error={error && !comment.trim() ? error : null}>
        <Textarea value={comment} onChange={(event) => setComment(event.target.value)} placeholder="Причина корректировки" />
      </Field>
    </Modal>
  );
}
