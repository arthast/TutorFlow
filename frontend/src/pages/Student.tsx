import { useMemo, useState, type FormEvent } from "react";
import {
  api,
  openFile,
  reports,
  type Assignment,
  type AssignmentDetail,
  type FileMeta,
  type Lesson,
  type Receipt,
  type StudentDashboard,
  type StudentSummary,
} from "../api";
import { Card, ErrorMsg, FileChips, ListState, Notice, NotificationsCard, StatusPill, TopBar, fmtDate, useAsync } from "../ui";

const TO_SUBMIT = new Set(["assigned", "needs_fix"]);

export default function Student() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);

  // teacher_id для чека берём из занятий/ДЗ ученика (баланса у ученика нет).
  const teacherIds = useMemo(() => {
    const ids = new Set<string>();
    (lessons.data ?? []).forEach((l) => ids.add(l.teacher_id));
    (assignments.data ?? []).forEach((a) => ids.add(a.teacher_id));
    return [...ids];
  }, [lessons.data, assignments.data]);

  const toSubmit = (assignments.data ?? []).filter((a) => TO_SUBMIT.has(a.status)).length;
  const report = dashboard.data;
  const activity = sumActivity(report?.summaries ?? []);

  return (
    <>
      <TopBar />
      <div className="container">
        <div className="metrics">
          <Metric label="Долг" value={money(report?.total_debt_amount, currencyOf(report))} />
          <Metric label="Переплата" value={money(report?.total_overpaid_amount, currencyOf(report))} />
          <Metric label="Чеки на проверке" value={`${report?.pending_receipts_count ?? 0} / ${money(report?.pending_receipts_amount, currencyOf(report))}`} />
          <Metric label="Ближайшие занятия" value={report ? activity.upcoming : (lessons.data ?? []).length} />
          <Metric label="ДЗ к сдаче" value={report ? activity.submitted : toSubmit} />
        </div>

        <div className="grid">
          <StudentDashboardCard dashboard={dashboard} lessons={lessons.data ?? []} />
          <NotificationsCard />
          <AssignmentsCard assignments={assignments} onChanged={dashboard.reload} />
          <ReceiptCard teacherIds={teacherIds} onSent={() => { receipts.reload(); dashboard.reload(); }} />
          <ReceiptsListCard receipts={receipts} />
          <LessonsCard lessons={lessons} />
          <PasswordCard />
        </div>
      </div>
    </>
  );
}

function Metric({ label, value }: { label: string; value: number | string }) {
  return (
    <div className="metric">
      <div className="label">{label}</div>
      <div className="value">{value}</div>
    </div>
  );
}

type Async<T> = ReturnType<typeof useAsync<T>>;

function money(value?: number, currency = "RUB"): string {
  if (typeof value !== "number") return "—";
  return `${Math.round(value)} ${currency}`;
}

function lessonInterval(startsAt?: string, endsAt?: string): string {
  if (!startsAt) return "—";
  const start = fmtDate(startsAt);
  if (!endsAt) return start;
  const end = new Date(endsAt);
  if (isNaN(end.getTime())) return `${start} - ${endsAt}`;
  return `${start} - ${end.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" })}`;
}

function currencyOf(dashboard: StudentDashboard | null): string {
  return dashboard?.summaries.find((s) => s.finance.currency)?.finance.currency ?? "RUB";
}

function sumActivity(summaries: StudentSummary[]) {
  return summaries.reduce(
    (acc, item) => ({
      upcoming: acc.upcoming + item.activity.upcoming_lessons_count,
      completed: acc.completed + item.activity.completed_lessons_count,
      cancelled: acc.cancelled + item.activity.cancelled_lessons_count,
      activeAssignments: acc.activeAssignments + item.activity.active_assignments_count,
      submitted: acc.submitted + item.activity.submitted_assignments_count,
      reviewed: acc.reviewed + item.activity.reviewed_assignments_count,
    }),
    { upcoming: 0, completed: 0, cancelled: 0, activeAssignments: 0, submitted: 0, reviewed: 0 },
  );
}

function StudentDashboardCard({
  dashboard,
  lessons,
}: {
  dashboard: Async<StudentDashboard>;
  lessons: Lesson[];
}) {
  const data = dashboard.data;
  const activity = sumActivity(data?.summaries ?? []);
  const currency = currencyOf(data);
  return (
    <Card title="Мой dashboard">
      <div className="card-tools">
        <button className="small" onClick={dashboard.reload} disabled={dashboard.loading}>
          {dashboard.loading ? "Обновление…" : "Обновить"}
        </button>
        <span className="hint">Обновлено: {fmtDate(data?.updated_at) || "—"}</span>
      </div>
      {dashboard.error && <ErrorMsg error={dashboard.error} />}
      {dashboard.loading && !data && <p className="hint">Загрузка…</p>}
      {data && (
        <>
          <div className="summary-grid summary-grid-wide">
            <span>Должен: <strong>{money(data.total_debt_amount, currency)}</strong></span>
            <span>Переплата: <strong>{money(data.total_overpaid_amount, currency)}</strong></span>
            <span>На проверке: {data.pending_receipts_count} чек(ов) / {money(data.pending_receipts_amount, currency)}</span>
            <span>Ближайшие занятия: {activity.upcoming}</span>
            <span>Проведённые занятия: {activity.completed}</span>
            <span>Активные ДЗ: {activity.activeAssignments}</span>
            <span>Сданные ДЗ: {activity.submitted}</span>
            <span>Проверенные ДЗ: {activity.reviewed}</span>
          </div>
          {data.summaries.map((summary) => (
            <StudentTeacherSummary
              key={summary.teacher_id}
              summary={summary}
              lessons={lessons}
            />
          ))}
          {data.summaries.length === 0 && <p className="hint">Dashboard пока пуст.</p>}
        </>
      )}
    </Card>
  );
}

function StudentTeacherSummary({
  summary,
  lessons,
}: {
  summary: StudentSummary;
  lessons: Lesson[];
}) {
  const nextLesson = lessons.find(
    (lesson) =>
      lesson.teacher_id === summary.teacher_id &&
      lesson.status === "scheduled" &&
      lesson.starts_at === summary.activity.next_lesson_at,
  );
  const teacherName = summary.teacher_name || `Преподаватель ${summary.teacher_id.slice(0, 8)}`;
  return (
    <div className="summary-row">
      <div>
        <div className="summary-title">{teacherName}</div>
        <div className="summary-grid">
          <span>Долг: <strong>{money(summary.finance.debt_amount, summary.finance.currency)}</strong></span>
          <span>Переплата: <strong>{money(summary.finance.overpaid_amount, summary.finance.currency)}</strong></span>
          <span>Чеки: {summary.finance.pending_receipts_count} / {money(summary.finance.pending_receipts_amount, summary.finance.currency)}</span>
          <span>Последняя оплата: {fmtDate(summary.finance.last_payment_at) || "—"}</span>
          <span>Следующее занятие: {lessonInterval(summary.activity.next_lesson_at, nextLesson?.ends_at)}</span>
          <span>Последнее занятие: {fmtDate(summary.activity.last_lesson_at) || "—"}</span>
        </div>
      </div>
      <div className="hint">обн. {fmtDate(summary.updated_at) || "—"}</div>
    </div>
  );
}

function LessonsCard({ lessons }: { lessons: Async<Lesson[]> }) {
  return (
    <Card title="Мои занятия">
      {(lessons.data ?? []).map((l) => (
        <div key={l.id}>
          <div className="row">
            <span>{l.topic || "Занятие"} · {lessonInterval(l.starts_at, l.ends_at)}</span>
            <span className="btn-group" style={{ alignItems: "center" }}>
              {typeof l.price === "number" && <span className="muted">{Math.round(l.price)} ₽</span>}
              <StatusPill status={l.status} />
            </span>
          </div>
          <FileChips fileIds={l.file_ids} label="Материалы урока" />
        </div>
      ))}
      <ListState query={lessons} empty="Занятий пока нет." />
    </Card>
  );
}

function AssignmentsCard({ assignments, onChanged }: { assignments: Async<Assignment[]>; onChanged: () => void }) {
  const [openId, setOpenId] = useState<string | null>(null);
  return (
    <Card title="Мои домашние задания">
      {(assignments.data ?? []).map((a) => (
        <div key={a.id}>
          <div className="row">
            <button className="small" onClick={() => setOpenId(openId === a.id ? null : a.id)} style={{ flex: 1, textAlign: "left" }}>
              {a.title}
            </button>
            <StatusPill status={a.status} />
          </div>
          {openId === a.id && <SubmitView id={a.id} onChange={() => { assignments.reload(); onChanged(); }} />}
        </div>
      ))}
      <ListState query={assignments} empty="Заданий пока нет." />
    </Card>
  );
}

function SubmitView({ id, onChange }: { id: string; onChange: () => void }) {
  const detail = useAsync<AssignmentDetail>(() => api.get(`/assignments/${id}`), [id]);
  const [text, setText] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      const fileIds: string[] = [];
      for (const f of files) {
        const form = new FormData();
        form.append("file", f);
        form.append("purpose", "submission_file");
        const meta = await api.upload<FileMeta>("/files", form);
        fileIds.push(meta.id);
      }
      await api.post(`/assignments/${id}/submit`, {
        text_answer: text || undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      setNotice("Решение отправлено.");
      setText(""); setFiles([]);
      detail.reload();
      onChange();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  const d = detail.data;
  return (
    <div style={{ padding: "8px 0 12px", borderTop: "0.5px solid var(--border)" }}>
      <ErrorMsg error={error} />
      <Notice text={notice} />
      {detail.loading && !d && <p className="hint">Загрузка…</p>}
      {d?.description && <p className="muted">{d.description}</p>}
      <FileChips fileIds={d?.file_ids} label="Материалы ДЗ" />
      {(d?.submissions ?? []).map((s) => <FileChips key={s.id} fileIds={s.file_ids} label="Мои файлы решения" />)}
      {(d?.comments ?? []).map((c) => (
        <div className="row" key={c.id}><span className="muted">Комментарий: {c.text}</span></div>
      ))}
      <form onSubmit={submit}>
        <div className="field"><label>Текст решения<textarea placeholder="Текст решения" value={text} onChange={(e) => setText(e.target.value)} /></label></div>
        <div className="field">
          <label>Файлы решения (можно несколько)
            <input type="file" multiple onChange={(e) => setFiles(e.target.files ? Array.from(e.target.files) : [])} />
          </label>
        </div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Отправка…" : "Отправить решение"}</button>
      </form>
    </div>
  );
}

function ReceiptCard({ teacherIds, onSent }: { teacherIds: string[]; onSent: () => void }) {
  const [teacherId, setTeacherId] = useState("");
  const [amount, setAmount] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const effectiveTeacher = teacherId || (teacherIds.length === 1 ? teacherIds[0] : "");

  async function send(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setNotice(null);
    if (!effectiveTeacher) { setError("Не удалось определить преподавателя"); return; }
    if (!file) { setError("Прикрепите файл чека"); return; }
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
      setNotice("Чек отправлен — ждёт подтверждения преподавателя.");
      setAmount(""); setFile(null);
      (e.target as HTMLFormElement).reset();
      onSent();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Загрузить чек оплаты">
      <ErrorMsg error={error} />
      <Notice text={notice} />
      <form onSubmit={send}>
        {teacherIds.length > 1 && (
          <div className="field">
            <label>Преподаватель
              <select value={teacherId} onChange={(e) => setTeacherId(e.target.value)} required>
                <option value="">— выбрать —</option>
                {teacherIds.map((id) => <option key={id} value={id}>{id.slice(0, 8)}…</option>)}
              </select>
            </label>
          </div>
        )}
        <div className="field"><label>Сумма ₽<input type="number" min="0" placeholder="сумма" value={amount} onChange={(e) => setAmount(e.target.value)} required /></label></div>
        <div className="field"><label>Файл чека<input type="file" onChange={(e) => setFile(e.target.files?.[0] ?? null)} required /></label></div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Отправка…" : "Отправить чек"}</button>
      </form>
      <p className="hint">Это заявка об оплате: баланс/долг ведёт преподаватель и подтверждает чек вручную.</p>
    </Card>
  );
}

function ReceiptsListCard({ receipts }: { receipts: Async<Receipt[]> }) {
  const [error, setError] = useState<string | null>(null);
  return (
    <Card title="Мои чеки">
      <ErrorMsg error={error} />
      {(receipts.data ?? []).map((r) => (
        <div className="row" key={r.id}>
          <span>{Math.round(r.amount)} ₽ · {fmtDate(r.submitted_at)}</span>
          <span className="btn-group" style={{ alignItems: "center" }}>
            <button className="small" onClick={() => openFile(r.file_id).catch((e) => setError((e as Error).message))}>Файл</button>
            <StatusPill status={r.status} />
          </span>
        </div>
      ))}
      <ListState query={receipts} empty="Чеков пока нет." />
    </Card>
  );
}

function PasswordCard() {
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function change(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      await api.post("/auth/change-password", { current_password: current, new_password: next });
      setNotice("Пароль обновлён.");
      setCurrent(""); setNext("");
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Сменить пароль">
      <ErrorMsg error={error} />
      <Notice text={notice} />
      <form onSubmit={change}>
        <div className="field"><label>Текущий пароль<input type="password" placeholder="текущий пароль" value={current} onChange={(e) => setCurrent(e.target.value)} required /></label></div>
        <div className="field"><label>Новый пароль (мин. 8)<input type="password" placeholder="новый пароль" value={next} onChange={(e) => setNext(e.target.value)} minLength={8} required /></label></div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Обновление…" : "Обновить"}</button>
      </form>
    </Card>
  );
}
