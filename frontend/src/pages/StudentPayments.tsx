import { useMemo, useState, type FormEvent } from "react";
import {
  api,
  reports,
  type Assignment,
  type FileMeta,
  type Lesson,
  type StudentDashboard,
} from "../api";
import { AppShell, Card, ErrorMsg, Icon, Notice, useAsync } from "../ui";
import { money, studentNav } from "./studentNav";

export default function StudentPayments() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const [noticeTick, setNoticeTick] = useState(0);

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

  return (
    <AppShell
      title="Оплата"
      subtitle="Долг и отправка чеков"
      navSection="Учёба"
      accent="student"
      navItems={studentNav("payments", {
        lessons: upcomingLessons,
        assignments: activeAssignments,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container">
        <div className="metrics">
          <Metric icon="account_balance_wallet" label="Долг" value={money(dashboard.data?.total_debt_amount, currency)} />
          <Metric icon="payments" label="Переплата" value={money(dashboard.data?.total_overpaid_amount, currency)} />
          <Metric icon="receipt_long" label="Чеки на проверке" value={`${dashboard.data?.pending_receipts_count ?? 0} / ${money(dashboard.data?.pending_receipts_amount, currency)}`} />
        </div>

        <div className="dashboard-grid">
          <Card title="По преподавателям" icon="account_balance_wallet">
            {(dashboard.data?.summaries ?? []).map((summary) => (
              <div className="summary-row" key={summary.teacher_id}>
                <div>
                  <div className="summary-title">{summary.teacher_name || summary.teacher_id.slice(0, 8)}</div>
                  <div className="summary-grid">
                    <span>Долг: {money(summary.finance.debt_amount, summary.finance.currency)}</span>
                    <span>Переплата: {money(summary.finance.overpaid_amount, summary.finance.currency)}</span>
                    <span>На проверке: {summary.finance.pending_receipts_count}</span>
                  </div>
                </div>
              </div>
            ))}
            {dashboard.data && dashboard.data.summaries.length === 0 && <p className="hint">Финансовых данных пока нет.</p>}
          </Card>
          <ReceiptUploadCard
            key={noticeTick}
            teachers={teacherOptions}
            onSent={() => {
              dashboard.reload();
              setNoticeTick((value) => value + 1);
            }}
          />
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

function ReceiptUploadCard({ teachers, onSent }: { teachers: Array<{ id: string; name: string }>; onSent: () => void }) {
  const [teacherId, setTeacherId] = useState("");
  const [amount, setAmount] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const effectiveTeacher = teacherId || (teachers.length === 1 ? teachers[0].id : "");

  async function send(event: FormEvent) {
    event.preventDefault();
    setError(null);
    setNotice(null);
    if (!effectiveTeacher) {
      setError("Выберите преподавателя.");
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
        amount: Number(amount),
      });
      setNotice("Чек отправлен.");
      setAmount("");
      setFile(null);
      (event.target as HTMLFormElement).reset();
      onSent();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Загрузить чек" icon="upload_file">
      <ErrorMsg error={error} />
      <Notice text={notice} />
      <form onSubmit={send}>
        <div className="field">
          <label>Преподаватель
            <span className="select-wrap">
              <select value={teacherId} onChange={(event) => setTeacherId(event.target.value)} required={teachers.length !== 1}>
                <option value="">— выбрать —</option>
                {teachers.map((teacher) => (
                  <option key={teacher.id} value={teacher.id}>{teacher.name}</option>
                ))}
              </select>
              <Icon name="expand_more" />
            </span>
          </label>
        </div>
        <div className="field"><label>Сумма ₽<input type="number" min="0" value={amount} onChange={(event) => setAmount(event.target.value)} required /></label></div>
        <div className="field"><label>Файл<input type="file" onChange={(event) => setFile(event.target.files?.[0] ?? null)} required /></label></div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Отправка..." : "Отправить чек"}</button>
      </form>
    </Card>
  );
}
