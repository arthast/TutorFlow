import { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import {
  api,
  reports,
  type StudentLink,
  type TeacherDashboard,
  type Transaction,
} from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, StatusPill, useAsync } from "../ui";
import { money, teacherNav } from "./teacherNav";

function transactionLabel(type: string): string {
  if (type === "payment") return "Платёж";
  if (type === "correction") return "Коррекция";
  if (type === "refund") return "Возврат";
  return "Начисление";
}

export default function TeacherFinance() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const [studentId, setStudentId] = useState("");
  const [transactions, setTransactions] = useState<Transaction[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [loadingTx, setLoadingTx] = useState(false);

  const selectedSummary = dashboard.data?.students.find((summary) => summary.student_id === studentId);

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

  return (
    <AppShell
      title="Финансы"
      subtitle="Долги, переплаты и журнал операций"
      navSection="Работа"
      navItems={teacherNav("finance", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={<Link className="primary-action" to="/teacher/receipts"><Icon name="receipt_long" /><span>Чеки</span></Link>}
    >
      <div className="container">
        <div className="metrics">
          <Metric icon="account_balance_wallet" label="Общий долг" value={money(dashboard.data?.total_debt_amount)} />
          <Metric icon="payments" label="Переплаты" value={money(dashboard.data?.total_overpaid_amount)} />
          <Metric icon="receipt_long" label="Чеки на проверке" value={dashboard.data?.pending_receipts_count ?? "-"} />
          <Metric icon="priority_high" label="Должников" value={dashboard.data?.students_with_debt_count ?? "-"} />
        </div>

        <div className="dashboard-grid">
          <Card title="Ученики и баланс" icon="group">
            {(dashboard.data?.students ?? []).map((summary) => (
              <button
                className={"resource-row resource-button" + (studentId === summary.student_id ? " active" : "")}
                key={summary.student_id}
                onClick={() => setStudentId(summary.student_id)}
                type="button"
              >
                <div className="resource-icon"><Icon name="person" /></div>
                <div className="resource-main">
                  <div className="summary-title">{summary.student_name || summary.student_id.slice(0, 8)}</div>
                  <div className="summary-grid">
                    <span>Долг: {money(summary.finance.debt_amount, summary.finance.currency)}</span>
                    <span>Переплата: {money(summary.finance.overpaid_amount, summary.finance.currency)}</span>
                    <span>Чеки: {summary.finance.pending_receipts_count}</span>
                  </div>
                </div>
                {summary.finance.debt_amount > 0 && <StatusPill status="pending_review" />}
              </button>
            ))}
            {dashboard.loading && !dashboard.data && <p className="hint">Загрузка...</p>}
            {dashboard.data && dashboard.data.students.length === 0 && <p className="hint">Финансовых данных пока нет.</p>}
          </Card>

          <Card title="Журнал операций" icon="receipt_long">
            <div className="card-tools">
              <select value={studentId} onChange={(event) => setStudentId(event.target.value)}>
                <option value="">— выбрать ученика —</option>
                {(students.data ?? []).map((student) => (
                  <option key={student.id} value={student.student_id}>{student.display_name}</option>
                ))}
              </select>
            </div>
            {selectedSummary && (
              <div className="summary-grid summary-grid-wide">
                <span>Долг: <strong>{money(selectedSummary.finance.debt_amount, selectedSummary.finance.currency)}</strong></span>
                <span>Переплата: <strong>{money(selectedSummary.finance.overpaid_amount, selectedSummary.finance.currency)}</strong></span>
              </div>
            )}
            <ErrorMsg error={error} />
            {loadingTx && <p className="hint">Загрузка операций...</p>}
            {transactions.map((transaction) => (
              <div className="transaction-row" key={transaction.id}>
                <div className="transaction-icon"><Icon name={transaction.type === "payment" ? "check_circle" : transaction.type === "correction" ? "tune" : "school"} /></div>
                <div>
                  <div className="summary-title">{transactionLabel(transaction.type)}</div>
                  <div className="muted">{transaction.comment || (transaction.created_at ? new Date(transaction.created_at).toLocaleString("ru-RU") : "-")}</div>
                </div>
                <strong className={transaction.amount >= 0 ? "amount-positive" : "amount-negative"}>
                  {transaction.amount > 0 ? "+" : ""}{money(transaction.amount, transaction.currency)}
                </strong>
              </div>
            ))}
            <ListState query={{ data: studentId ? transactions : [], loading: loadingTx, error, reload: () => undefined }} empty={studentId ? "Операций пока нет." : "Выберите ученика."} />
          </Card>
        </div>
      </div>
    </AppShell>
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
