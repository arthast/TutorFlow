import { useEffect, useMemo, useState } from "react";
import { Link } from "react-router-dom";
import {
  api,
  reports,
  type StudentLink,
  type StudentSummary,
  type TeacherDashboard,
  type Transaction,
} from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, useAsync } from "../ui";
import { initials, money, teacherNav } from "./teacherNav";

type FinanceFilter = "all" | "debt" | "pending";

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

function studentName(students: StudentLink[], summary: StudentSummary): string {
  return summary.student_name || students.find((student) => student.student_id === summary.student_id)?.display_name || summary.student_id.slice(0, 8);
}

function studentSubject(students: StudentLink[], studentId: string): string {
  const student = students.find((item) => item.student_id === studentId);
  return student?.subject || student?.goal || "Предмет не указан";
}

function financeTag(summary: StudentSummary) {
  if (summary.finance.pending_receipts_count > 0) {
    return { label: "чек на проверке", className: "finance-tag-warning" };
  }
  if (summary.finance.overpaid_amount > 0) {
    return { label: "переплата", className: "finance-tag-success" };
  }
  if (summary.finance.debt_amount <= 0) {
    return { label: "оплачено", className: "finance-tag-muted" };
  }
  return { label: "есть долг", className: "finance-tag-warning" };
}

export default function TeacherFinance() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const [filter, setFilter] = useState<FinanceFilter>("all");
  const [studentId, setStudentId] = useState("");
  const [transactions, setTransactions] = useState<Transaction[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [loadingTx, setLoadingTx] = useState(false);

  const studentLinks = students.data ?? [];
  const summaries = dashboard.data?.students ?? [];

  useEffect(() => {
    if (!studentId && summaries.length > 0) {
      setStudentId(summaries[0].student_id);
    }
  }, [studentId, summaries]);

  useEffect(() => {
    if (!studentId) {
      setTransactions([]);
      return;
    }
    setLoadingTx(true);
    setError(null);
    api
      .get<Transaction[]>(`/students/${studentId}/transactions`)
      .then(setTransactions)
      .catch((err) => setError((err as Error).message))
      .finally(() => setLoadingTx(false));
  }, [studentId]);

  const visibleSummaries = useMemo(() => summaries.filter((summary) => {
    if (filter === "debt") return summary.finance.debt_amount > 0;
    if (filter === "pending") return summary.finance.pending_receipts_count > 0;
    return true;
  }), [filter, summaries]);

  const selectedSummary = summaries.find((summary) => summary.student_id === studentId);
  const paidAmount = transactions
    .filter((transaction) => transaction.type === "payment")
    .reduce((sum, transaction) => sum + Math.abs(transaction.amount), 0);
  const accruedAmount = transactions
    .filter((transaction) => transaction.type === "charge")
    .reduce((sum, transaction) => sum + Math.abs(transaction.amount), 0);

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
      actions={<Link className="button-link" to="/teacher/receipts"><Icon name="receipt_long" /><span>Перейти к чекам</span></Link>}
    >
      <div className="container">
        <div className="finance-kpi-grid">
          <div className="finance-hero-card">
            <div className="label"><Icon name="account_balance_wallet" />Общий долг учеников</div>
            <div className="value">{money(dashboard.data?.total_debt_amount)}</div>
            <div className="hint">{dashboard.data?.students_with_debt_count ?? 0} учеников с долгом · переплат {money(dashboard.data?.total_overpaid_amount)}</div>
          </div>
          <Metric icon="trending_up" label="Начислено" value={money(accruedAmount)} hint={selectedSummary ? studentName(studentLinks, selectedSummary) : "выбранный ученик"} />
          <Metric icon="check_circle" label="Оплачено" value={money(paidAmount)} hint="подтверждённые платежи" success />
          <Metric icon="hourglass_top" label="Чеки на проверке" value={money(dashboard.data?.pending_receipts_amount)} hint={`${dashboard.data?.pending_receipts_count ?? 0} ждут подтверждения`} warning />
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
              <span>Долг</span>
              <span>Статус оплат</span>
            </div>
            {visibleSummaries.map((summary) => {
              const name = studentName(studentLinks, summary);
              const tag = financeTag(summary);
              return (
                <button
                  className={"finance-student-row" + (studentId === summary.student_id ? " active" : "")}
                  key={summary.student_id}
                  onClick={() => setStudentId(summary.student_id)}
                  type="button"
                >
                  <span className="finance-student-main">
                    <span className="avatar">{initials(name)}</span>
                    <span>
                      <strong>{name}</strong>
                      <em>{studentSubject(studentLinks, summary.student_id)}</em>
                    </span>
                  </span>
                  <strong className={summary.finance.debt_amount > 0 ? "finance-debt" : "finance-zero"}>
                    {summary.finance.debt_amount > 0 ? money(summary.finance.debt_amount, summary.finance.currency) : "0 RUB"}
                  </strong>
                  <span className={"finance-tag " + tag.className}>{tag.label}</span>
                </button>
              );
            })}
            {dashboard.loading && !dashboard.data && <p className="hint">Загрузка...</p>}
            {dashboard.data && visibleSummaries.length === 0 && <p className="hint">По фильтру ничего нет.</p>}
          </Card>

          <div className="dashboard-column">
            <Card title="Последние операции" icon="history">
              <div className="card-tools">
                <select value={studentId} onChange={(event) => setStudentId(event.target.value)}>
                  <option value="">— выбрать ученика —</option>
                  {summaries.map((summary) => (
                    <option key={summary.student_id} value={summary.student_id}>{studentName(studentLinks, summary)}</option>
                  ))}
                </select>
              </div>
              <ErrorMsg error={error} />
              {loadingTx && <p className="hint">Загрузка операций...</p>}
              {transactions.map((transaction) => (
                <div className="finance-operation-row" key={transaction.id}>
                  <div className="transaction-icon"><Icon name={transactionIcon(transaction.type)} /></div>
                  <div>
                    <div className="summary-title">{transactionLabel(transaction.type)}</div>
                    <div className="muted">{transaction.comment || (transaction.created_at ? new Date(transaction.created_at).toLocaleString("ru-RU") : "-")}</div>
                  </div>
                  <strong className={transaction.type === "payment" || transaction.amount < 0 ? "amount-negative" : ""}>
                    {transaction.amount > 0 ? "+" : "−"}{money(Math.abs(transaction.amount), transaction.currency)}
                  </strong>
                </div>
              ))}
              <ListState query={{ data: studentId ? transactions : [], loading: loadingTx, error, reload: () => undefined }} empty={studentId ? "Операций пока нет." : "Выберите ученика."} />
            </Card>

            <div className="pending-receipts-card">
              <div>
                <Icon name="hourglass_top" />
                <strong>{dashboard.data?.pending_receipts_count ?? 0} чека на проверке</strong>
              </div>
              <p>На сумму {money(dashboard.data?.pending_receipts_amount)}. Подтвердите оплату, чтобы уменьшить долг учеников.</p>
              <Link className="primary-action" to="/teacher/receipts"><Icon name="receipt_long" />Перейти к чекам</Link>
            </div>
          </div>
        </div>
      </div>
    </AppShell>
  );
}

function Metric({
  icon,
  label,
  value,
  hint,
  success,
  warning,
}: {
  icon: string;
  label: string;
  value: number | string;
  hint: string;
  success?: boolean;
  warning?: boolean;
}) {
  return (
    <div className="metric">
      <div className="label"><Icon name={icon} />{label}</div>
      <div className={"value" + (success ? " metric-success" : warning ? " metric-warning" : "")}>{value}</div>
      <div className="hint">{hint}</div>
    </div>
  );
}
