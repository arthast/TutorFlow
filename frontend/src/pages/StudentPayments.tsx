import { useMemo, useState, type FormEvent } from "react";
import {
  api,
  reports,
  type Assignment,
  type FileMeta,
  type Lesson,
  type Receipt,
  type StudentDashboard,
} from "../api";
import {
  AppShell,
  Button,
  Card,
  ErrorMsg,
  ErrorState,
  Field,
  Icon,
  Input,
  Metric,
  Select,
  SkeletonRows,
  useAsync,
  useToast,
} from "../ui";
import { money as formatMoney, signedMoney as formatSignedBalance, studentNav } from "./studentNav";
import { StudentReceiptHistory } from "./StudentReceiptHistory";

export default function StudentPayments() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const [error, setError] = useState<string | null>(null);

  const teacherOptions = useMemo(() => {
    const names = new Map<string, string>();
    (dashboard.data?.summaries ?? []).forEach((summary) => {
      names.set(summary.teacher_id, summary.teacher_name || summary.teacher_id.slice(0, 8));
    });
    (lessons.data ?? []).forEach((lesson) => {
      if (!names.has(lesson.teacher_id)) names.set(lesson.teacher_id, lesson.teacher_id.slice(0, 8));
    });
    (assignments.data ?? []).forEach((assignment) => {
      if (!names.has(assignment.teacher_id)) names.set(assignment.teacher_id, assignment.teacher_id.slice(0, 8));
    });
    return [...names].map(([id, name]) => ({ id, name }));
  }, [assignments.data, dashboard.data?.summaries, lessons.data]);

  const activeAssignments = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.upcoming_lessons_count, 0) ?? 0;
  const currency = dashboard.data?.summaries.find((summary) => summary.finance.currency)?.finance.currency ?? "RUB";
  const netDebt = (dashboard.data?.total_debt_amount ?? 0) - (dashboard.data?.total_overpaid_amount ?? 0);
  const sortedReceipts = useMemo(
    () => [...(receipts.data ?? [])].sort((a, b) => new Date(b.submitted_at ?? 0).getTime() - new Date(a.submitted_at ?? 0).getTime()),
    [receipts.data],
  );

  function reloadAfterSend() {
    dashboard.reload();
    receipts.reload();
  }

  return (
    <AppShell
      title="Оплата"
      subtitle="Заявите оплату и приложите чек"
      navSection="Учёба"
      accent="student"
      navItems={studentNav("payments", {
        lessons: upcomingLessons,
        assignments: activeAssignments,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container student-payment-layout">
        <div className="student-payment-side">
          <div className="balance-hero student-payment-hero">
            <div className="label">
              <Icon name="account_balance_wallet" />
              Мой долг
              <span className="balance-lock"><Icon name="lock" />только просмотр</span>
            </div>
            <div className="value">{formatSignedBalance(netDebt, currency)}</div>
            <div className="hint">{netDebt < 0 ? "переплата у преподавателя" : "к оплате преподавателю"}</div>
            <div className="balance-foot">
              <div>
                <div className="balance-foot-label">Долг</div>
                <div className="balance-foot-value">{formatMoney(dashboard.data?.total_debt_amount, currency)}</div>
              </div>
              <div>
                <div className="balance-foot-label">Переплата</div>
                <div className="balance-foot-value">{formatMoney(dashboard.data?.total_overpaid_amount, currency)}</div>
              </div>
            </div>
          </div>

          {(dashboard.data?.pending_receipts_count ?? 0) > 0 && (
            <div className="pending-receipts-card student-pending-card">
              <div>
                <Icon name="hourglass_top" />
                <strong>На проверке: {formatMoney(dashboard.data?.pending_receipts_amount, currency)}</strong>
              </div>
              <p>{dashboard.data?.pending_receipts_count ?? 0} чеков ожидают подтверждения. Они пока не уменьшают долг.</p>
            </div>
          )}

          <ReceiptUploadCard
            teachers={teacherOptions}
            currency={currency}
            suggestedAmount={Math.max(0, netDebt)}
            onSent={reloadAfterSend}
          />
        </div>

        <div className="dashboard-column">
          <div className="metrics student-payment-metrics">
            <Metric icon="receipt_long" label="На проверке" value={dashboard.data?.pending_receipts_count ?? 0} sub={formatMoney(dashboard.data?.pending_receipts_amount, currency)} tone="warn" />
            <Metric icon="task_alt" label="Всего чеков" value={receipts.data?.length ?? 0} sub="история отправок" />
          </div>

          <Card title="История чеков" icon="receipt_long">
            <ErrorMsg error={error || dashboard.error} />
            {receipts.loading && !receipts.data ? (
              <SkeletonRows count={4} />
            ) : receipts.error ? (
              <ErrorState error={receipts.error} onRetry={receipts.reload} />
            ) : (
              <StudentReceiptHistory
                receipts={sortedReceipts}
                dashboard={dashboard.data}
                limit={5}
                emptyHint="Отправьте первый чек после оплаты."
                onError={setError}
              />
            )}
          </Card>
        </div>
      </div>
    </AppShell>
  );
}

function ReceiptUploadCard({
  teachers,
  currency,
  suggestedAmount,
  onSent,
}: {
  teachers: Array<{ id: string; name: string }>;
  currency: string;
  suggestedAmount: number;
  onSent: () => void;
}) {
  const toast = useToast();
  const [teacherId, setTeacherId] = useState("");
  const [amount, setAmount] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [fileKey, setFileKey] = useState(0);

  const effectiveTeacher = teacherId || (teachers.length === 1 ? teachers[0].id : "");
  const quickAmounts = [suggestedAmount, 3000, 5000].filter((value, index, list) => value > 0 && list.indexOf(value) === index);

  async function send(event: FormEvent) {
    event.preventDefault();
    setError(null);
    const numericAmount = Number(amount);
    if (!effectiveTeacher) {
      setError("Выберите преподавателя.");
      return;
    }
    if (!Number.isFinite(numericAmount) || numericAmount <= 0) {
      setError("Сумма должна быть больше 0.");
      return;
    }
    if (!file) {
      setError("Прикрепите файл чека.");
      return;
    }
    setBusy(true);
    try {
      const form = new FormData();
      form.append("file", file);
      form.append("purpose", "payment_receipt");
      const meta = await api.upload<FileMeta>("/files", form);
      await api.post("/payments/receipts", {
        teacher_id: effectiveTeacher,
        file_id: meta.id,
        amount: numericAmount,
        currency,
      });
      toast({ tone: "warning", title: "Чек отправлен на проверку", body: "Долг не изменится до подтверждения преподавателем." });
      setAmount("");
      setFile(null);
      setFileKey((value) => value + 1);
      onSent();
    } catch (err) {
      setError((err as Error).message);
      toast({ tone: "danger", title: "Чек не отправлен", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Заявить оплату" icon="add_card">
      <p className="card-note">Укажите сумму и приложите чек. Эквайринга нет: преподаватель сверит чек вручную.</p>
      <ErrorMsg error={error} />
      <form className="payment-form" onSubmit={send}>
        <Field label="Преподаватель">
          <Select value={teacherId} onChange={(event) => setTeacherId(event.target.value)} required={teachers.length !== 1}>
            <option value="">Выберите преподавателя</option>
            {teachers.map((teacher) => (
              <option key={teacher.id} value={teacher.id}>{teacher.name}</option>
            ))}
          </Select>
        </Field>
        <Field label={`Сумма оплаты, ${currency}`} error={error?.startsWith("Сумма") ? error : null}>
          <Input type="number" min="0" step="1" value={amount} onChange={(event) => setAmount(event.target.value)} placeholder="0" required />
        </Field>
        {quickAmounts.length > 0 && (
          <div className="quick-amounts">
            {quickAmounts.map((value) => (
              <button type="button" key={value} onClick={() => setAmount(String(Math.round(value)))}>
                {formatMoney(value, currency)}
              </button>
            ))}
          </div>
        )}
        <Field label="Файл чека" error={error?.startsWith("Прикрепите") ? error : null}>
          <label className={"upload-drop" + (file ? " has-file" : "")}>
            <span className="dash-icon lg tone-warning"><Icon name={file ? "task_alt" : "upload_file"} /></span>
            <span>
              <strong>{file ? file.name : "Выберите файл"}</strong>
              <em>{file ? `${Math.max(1, Math.round(file.size / 1024))} КБ` : "PDF, JPG или PNG"}</em>
            </span>
            <Input key={fileKey} type="file" onChange={(event) => setFile(event.target.files?.[0] ?? null)} required />
          </label>
        </Field>
        <Button variant="primary" icon="send" loading={busy} block type="submit">Отправить на проверку</Button>
        <div className="info-note">
          <Icon name="info" />
          <span><strong>Баланс не изменится сразу.</strong> Pending-чек только ждёт проверки.</span>
        </div>
      </form>
    </Card>
  );
}
