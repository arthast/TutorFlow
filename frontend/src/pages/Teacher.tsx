import { useEffect, useState, type FormEvent } from "react";
import { Link } from "react-router-dom";
import {
  api,
  openFile,
  reports,
  type Assignment,
  type AssignmentDetail,
  type Balance,
  type CompleteLessonResponse,
  type FileMeta,
  type Lesson,
  type Receipt,
  type StudentLink,
  type StudentSummary,
  type TeacherDashboard,
  type Transaction,
} from "../api";
import { AppShell, Card, ErrorMsg, FileChips, Icon, ListState, Notice, NotificationsCard, StatusPill, fmtDate, useAsync } from "../ui";
import { ChatCard } from "../chat";
import { teacherNav } from "./teacherNav";

async function uploadAll(files: File[], purpose: string): Promise<string[]> {
  const ids: string[] = [];
  for (const f of files) {
    const form = new FormData();
    form.append("file", f);
    form.append("purpose", purpose);
    const meta = await api.upload<FileMeta>("/files", form);
    ids.push(meta.id);
  }
  return ids;
}

export default function Teacher() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const receipts = useAsync<Receipt[]>(
    () => api.get("/payments/receipts?status=pending_review"),
    [],
  );
  const [chargeRefresh, setChargeRefresh] = useState<{ studentId: string; seq: number } | null>(null);

  const studentList = students.data ?? [];
  const scheduled = (lessons.data ?? []).filter((l) => l.status === "scheduled").length;
  const report = dashboard.data;
  const pendingReceipts = report?.pending_receipts_count ?? (receipts.data ?? []).length;

  return (
    <AppShell
      title="Сводка"
      subtitle={new Date().toLocaleDateString("ru-RU", { weekday: "long", day: "numeric", month: "long" })}
      navSection="Работа"
      navItems={teacherNav("summary", {
        students: report?.students_count ?? studentList.length,
        lessons: report?.upcoming_lessons_count ?? scheduled,
        assignments: report?.pending_submissions_count,
        receipts: pendingReceipts,
      })}
      actions={
        <Link className="primary-action" to="/teacher/lessons">
          <Icon name="add" />
          <span>Новое занятие</span>
        </Link>
      }
    >
      <div className="container">
        <div className="metrics" id="summary">
          <Metric icon="group" label="Ученики" value={report?.students_count ?? studentList.length} />
          <Metric icon="calendar_month" label="Ближайшие занятия" value={report?.upcoming_lessons_count ?? scheduled} />
          <Metric icon="assignment_turned_in" label="ДЗ на проверке" value={report?.pending_submissions_count ?? "—"} />
          <Metric icon="receipt_long" label="Чеки на проверку" value={pendingReceipts} />
          <Metric icon="account_balance_wallet" label="Общий долг" value={money(report?.total_debt_amount, "RUB")} />
          <Metric icon="payments" label="Переплаты" value={money(report?.total_overpaid_amount, "RUB")} />
          <Metric icon="priority_high" label="Должников" value={report?.students_with_debt_count ?? "—"} />
        </div>

        <div className="grid">
          <TeacherDashboardCard dashboard={dashboard} />
          <NotificationsCard />
          <StudentsCard students={students} onChanged={dashboard.reload} />
          <LessonsCard
            lessons={lessons}
            students={studentList}
            onChargePending={(studentId) => setChargeRefresh({ studentId, seq: Date.now() })}
            onChanged={dashboard.reload}
          />
          <AssignmentsCard assignments={assignments} students={studentList} onChanged={dashboard.reload} />
          <FinanceCard
            receipts={receipts}
            students={studentList}
            chargeRefresh={chargeRefresh}
            onChanged={dashboard.reload}
          />
          <ChatCard
            contacts={studentList.map((s) => ({ id: s.student_id, name: s.display_name }))}
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

type Async<T> = ReturnType<typeof useAsync<T>>;

function money(value?: number, currency = "RUB"): string {
  if (typeof value !== "number") return "—";
  return `${Math.round(value)} ${currency}`;
}

function TeacherDashboardCard({ dashboard }: { dashboard: Async<TeacherDashboard> }) {
  const data = dashboard.data;
  return (
    <Card title="Сводка по ученикам" icon="monitoring">
      <div className="card-tools">
        <button className="small" onClick={dashboard.reload} disabled={dashboard.loading}>
          {dashboard.loading ? "Обновление…" : "Обновить"}
        </button>
        <span className="hint">Обновлено: {fmtDate(data?.updated_at) || "—"}</span>
      </div>
      {dashboard.error && <ErrorMsg error={dashboard.error} />}
      {dashboard.loading && !data && <p className="hint">Загрузка…</p>}
      {(data?.students ?? []).map((summary) => (
        <StudentSummaryRow key={summary.student_id} summary={summary} />
      ))}
      {data && data.students.length === 0 && <p className="hint">Пока нет данных по ученикам.</p>}
    </Card>
  );
}

function StudentSummaryRow({ summary }: { summary: StudentSummary }) {
  const finance = summary.finance;
  const activity = summary.activity;
  return (
    <div className="summary-row">
      <div>
        <div className="summary-title">{summary.student_name || summary.student_id.slice(0, 8)}</div>
        <div className="summary-grid">
          <span>Долг: <strong>{money(finance.debt_amount, finance.currency)}</strong></span>
          <span>Переплата: <strong>{money(finance.overpaid_amount, finance.currency)}</strong></span>
          <span>Чеки: {finance.pending_receipts_count} / {money(finance.pending_receipts_amount, finance.currency)}</span>
          <span>Занятия: {activity.upcoming_lessons_count} ближайш. / {activity.completed_lessons_count} провед.</span>
          <span>ДЗ: {activity.active_assignments_count} активн. / {activity.submitted_assignments_count} сдано / {activity.reviewed_assignments_count} провер.</span>
        </div>
      </div>
      <div className="hint">обн. {fmtDate(summary.updated_at) || "—"}</div>
    </div>
  );
}

function StudentsCard({ students, onChanged }: { students: Async<StudentLink[]>; onChanged: () => void }) {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [name, setName] = useState("");
  const [subject, setSubject] = useState("");
  const [rate, setRate] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function create(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      await api.post("/students", {
        email,
        password,
        display_name: name,
        subject: subject || undefined,
        hourly_rate: rate ? Number(rate) : undefined,
      });
      setNotice(`Ученик «${name}» создан. Передайте ему email и временный пароль.`);
      setEmail(""); setPassword(""); setName(""); setSubject(""); setRate("");
      students.reload();
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Ученики" icon="group" id="students">
      {(students.data ?? []).map((s) => (
        <div className="row" key={s.id}>
          <Link to={`/teacher/students/${s.student_id}`}>
            {s.display_name}{s.subject ? ` · ${s.subject}` : ""}
          </Link>
          <StatusPill status={s.status} />
        </div>
      ))}
      <ListState query={students} empty="Пока нет учеников." />

      <form onSubmit={create} style={{ marginTop: 12, borderTop: "0.5px solid var(--border)", paddingTop: 12 }}>
        <p className="section-title">Создать ученика (логин + временный пароль)</p>
        <ErrorMsg error={error} />
        <Notice text={notice} />
        <div className="field"><label>Email ученика<input placeholder="student@example.com" type="email" value={email} onChange={(e) => setEmail(e.target.value)} required /></label></div>
        <div className="field"><label>Временный пароль (мин. 8)<input placeholder="временный пароль" value={password} onChange={(e) => setPassword(e.target.value)} minLength={8} required /></label></div>
        <div className="field"><label>Имя<input placeholder="имя ученика" value={name} onChange={(e) => setName(e.target.value)} required /></label></div>
        <div className="field field-row">
          <label style={{ flex: 1 }}>Предмет<input placeholder="предмет" value={subject} onChange={(e) => setSubject(e.target.value)} /></label>
          <label style={{ width: 120 }}>Ставка ₽<input placeholder="ставка" type="number" min="0" value={rate} onChange={(e) => setRate(e.target.value)} /></label>
        </div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Создание…" : "Создать ученика"}</button>
      </form>
    </Card>
  );
}

function toIso(local: string): string {
  return new Date(local).toISOString();
}

function LessonsCard({
  lessons,
  students,
  onChargePending,
  onChanged,
}: {
  lessons: Async<Lesson[]>;
  students: StudentLink[];
  onChargePending: (studentId: string) => void;
  onChanged: () => void;
}) {
  const [studentId, setStudentId] = useState("");
  const [starts, setStarts] = useState("");
  const [ends, setEnds] = useState("");
  const [topic, setTopic] = useState("");
  const [price, setPrice] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [actingId, setActingId] = useState<string | null>(null);
  const [rescheduleId, setRescheduleId] = useState<string | null>(null);
  const [rStarts, setRStarts] = useState("");
  const [rEnds, setREnds] = useState("");

  function nameOf(id: string) {
    return students.find((s) => s.student_id === id)?.display_name ?? id.slice(0, 8);
  }

  async function reactivate(id: string) {
    setError(null);
    setNotice(null);
    setActingId(id);
    try {
      await api.post(`/lessons/${id}/reactivate`);
      setNotice("Занятие восстановлено.");
      lessons.reload();
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setActingId(null);
    }
  }

  async function reschedule(id: string, e: FormEvent) {
    e.preventDefault();
    setError(null);
    setNotice(null);
    setActingId(id);
    try {
      await api.post(`/lessons/${id}/reschedule`, {
        new_starts_at: toIso(rStarts),
        new_ends_at: toIso(rEnds),
      });
      setNotice("Занятие перенесено.");
      setRescheduleId(null); setRStarts(""); setREnds("");
      lessons.reload();
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setActingId(null);
    }
  }

  async function complete(id: string) {
    setError(null);
    setNotice(null);
    setActingId(id);
    try {
      const result = await api.post<CompleteLessonResponse>(`/lessons/${id}/complete`);
      lessons.reload();
      onChanged();
      if (result.charge_status === "pending") {
        setNotice("Занятие завершено. Начисление создаётся — баланс ученика обновится через пару секунд.");
        onChargePending(result.lesson.student_id);
      } else {
        setNotice("Занятие завершено.");
      }
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setActingId(null);
    }
  }

  async function cancel(id: string) {
    if (!window.confirm("Отменить это занятие? Действие необратимо.")) return;
    setError(null);
    setNotice(null);
    setActingId(id);
    try {
      await api.post(`/lessons/${id}/cancel`);
      lessons.reload();
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setActingId(null);
    }
  }

  async function create(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      const fileIds = await uploadAll(files, "lesson_material");
      await api.post("/lessons", {
        student_id: studentId,
        starts_at: toIso(starts),
        ends_at: toIso(ends),
        topic: topic || undefined,
        price: price ? Number(price) : undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      setNotice("Занятие создано.");
      setStarts(""); setEnds(""); setTopic(""); setPrice(""); setFiles([]);
      lessons.reload();
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Занятия" icon="calendar_month" id="lessons">
      {(lessons.data ?? []).map((l) => (
        <div key={l.id}>
          <div className="row">
            <span>
              {nameOf(l.student_id)} · {fmtDate(l.starts_at)}{l.topic ? ` · ${l.topic}` : ""}
              {" "}<StatusPill status={l.status} />
            </span>
            <span className="btn-group">
              {l.status === "scheduled" && (
                <>
                  <button className="small" disabled={actingId === l.id} onClick={() => complete(l.id)}>
                    {actingId === l.id ? "…" : "Завершить"}
                  </button>
                  <button className="small" disabled={actingId === l.id}
                          onClick={() => { setRescheduleId(rescheduleId === l.id ? null : l.id); setRStarts(""); setREnds(""); }}>
                    Перенести
                  </button>
                  <button className="small" disabled={actingId === l.id} onClick={() => cancel(l.id)}>Отмена</button>
                </>
              )}
              {l.status === "completed" && (
                <button className="small" disabled={actingId === l.id} onClick={() => cancel(l.id)}>Отменить</button>
              )}
              {l.status === "cancelled" && (
                <button className="small" disabled={actingId === l.id} onClick={() => reactivate(l.id)}>Восстановить</button>
              )}
            </span>
          </div>
          {rescheduleId === l.id && (
            <form onSubmit={(e) => reschedule(l.id, e)} style={{ padding: "6px 0 10px" }}>
              <div className="field field-row">
                <label style={{ flex: 1 }}>Новое начало<input type="datetime-local" value={rStarts} onChange={(e) => setRStarts(e.target.value)} required /></label>
                <label style={{ flex: 1 }}>Новый конец<input type="datetime-local" value={rEnds} onChange={(e) => setREnds(e.target.value)} required /></label>
              </div>
              <button className="primary small" type="submit" disabled={actingId === l.id}>Перенести занятие</button>
            </form>
          )}
          <FileChips fileIds={l.file_ids} label="Материалы урока" />
        </div>
      ))}
      <ListState query={lessons} empty="Занятий пока нет." />
      <Notice text={notice} />

      <form id="new-lesson" onSubmit={create} style={{ marginTop: 12, borderTop: "0.5px solid var(--border)", paddingTop: 12 }}>
        <p className="section-title">Создать занятие</p>
        <ErrorMsg error={error} />
        <div className="field">
          <label>Ученик
            <select value={studentId} onChange={(e) => setStudentId(e.target.value)} required>
              <option value="">— ученик —</option>
              {students.map((s) => <option key={s.id} value={s.student_id}>{s.display_name}</option>)}
            </select>
          </label>
        </div>
        <div className="field"><label>Начало<input type="datetime-local" value={starts} onChange={(e) => setStarts(e.target.value)} required /></label></div>
        <div className="field"><label>Конец<input type="datetime-local" value={ends} onChange={(e) => setEnds(e.target.value)} required /></label></div>
        <div className="field field-row">
          <label style={{ flex: 1 }}>Тема<input placeholder="тема" value={topic} onChange={(e) => setTopic(e.target.value)} /></label>
          <label style={{ width: 130 }}>Цена ₽ (опц.)<input placeholder="из ставки" type="number" min="0" value={price} onChange={(e) => setPrice(e.target.value)} /></label>
        </div>
        <div className="field">
          <label>Материалы урока (можно несколько)
            <input type="file" multiple onChange={(e) => setFiles(e.target.files ? Array.from(e.target.files) : [])} />
          </label>
        </div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Создание…" : "Создать занятие"}</button>
      </form>
    </Card>
  );
}

function AssignmentsCard({
  assignments,
  students,
  onChanged,
}: {
  assignments: Async<Assignment[]>;
  students: StudentLink[];
  onChanged: () => void;
}) {
  const [studentId, setStudentId] = useState("");
  const [title, setTitle] = useState("");
  const [description, setDescription] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [openId, setOpenId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function create(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      const fileIds = await uploadAll(files, "assignment_attachment");
      await api.post("/assignments", {
        student_id: studentId,
        title,
        description: description || undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      setNotice("ДЗ создано.");
      setTitle(""); setDescription(""); setFiles([]);
      assignments.reload();
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Домашние задания" icon="assignment" id="assignments">
      {(assignments.data ?? []).map((a) => (
        <div key={a.id}>
          <div className="row">
            <button className="small" onClick={() => setOpenId(openId === a.id ? null : a.id)} style={{ flex: 1, textAlign: "left" }}>
              {a.title}
            </button>
            <span className="btn-group">
              <Link className="button-link small-link" to={`/teacher/assignments/${a.id}/review`}>Проверка</Link>
              <StatusPill status={a.status} />
            </span>
          </div>
          {openId === a.id && <AssignmentDetailView id={a.id} onChange={() => { assignments.reload(); onChanged(); }} />}
        </div>
      ))}
      <ListState query={assignments} empty="Заданий пока нет." />

      <form onSubmit={create} style={{ marginTop: 12, borderTop: "0.5px solid var(--border)", paddingTop: 12 }}>
        <p className="section-title">Создать ДЗ</p>
        <ErrorMsg error={error} />
        <Notice text={notice} />
        <div className="field">
          <label>Ученик
            <select value={studentId} onChange={(e) => setStudentId(e.target.value)} required>
              <option value="">— ученик —</option>
              {students.map((s) => <option key={s.id} value={s.student_id}>{s.display_name}</option>)}
            </select>
          </label>
        </div>
        <div className="field"><label>Название<input placeholder="название" value={title} onChange={(e) => setTitle(e.target.value)} required /></label></div>
        <div className="field"><label>Описание<textarea placeholder="описание" value={description} onChange={(e) => setDescription(e.target.value)} /></label></div>
        <div className="field">
          <label>Файлы к ДЗ (можно несколько)
            <input type="file" multiple onChange={(e) => setFiles(e.target.files ? Array.from(e.target.files) : [])} />
          </label>
        </div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Создание…" : "Создать ДЗ"}</button>
      </form>
    </Card>
  );
}

function AssignmentDetailView({ id, onChange }: { id: string; onChange: () => void }) {
  const detail = useAsync<AssignmentDetail>(() => api.get(`/assignments/${id}`), [id]);
  const [status, setStatus] = useState("reviewed");
  const [comment, setComment] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function review(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setBusy(true);
    try {
      await api.post(`/assignments/${id}/review`, { status, comment: comment || undefined });
      setComment("");
      detail.reload();
      onChange();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  async function addComment() {
    if (!comment.trim()) return;
    setError(null);
    setBusy(true);
    try {
      await api.post(`/assignments/${id}/comments`, { text: comment });
      setComment("");
      detail.reload();
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
      {detail.loading && !d && <p className="hint">Загрузка…</p>}
      {d?.description && <p className="muted">{d.description}</p>}
      <FileChips fileIds={d?.file_ids} label="Материалы ДЗ" />
      <p className="section-title">Решения</p>
      {(d?.submissions ?? []).map((s) => (
        <div key={s.id} style={{ padding: "6px 0", borderTop: "0.5px solid var(--border)" }}>
          <div className="row" style={{ border: "none", padding: 0 }}>
            <span className="muted">{s.text_answer || "(без текста)"}</span>
            <StatusPill status={s.status} />
          </div>
          <FileChips fileIds={s.file_ids} />
        </div>
      ))}
      {d && (d.submissions ?? []).length === 0 && <p className="hint">Решений ещё нет.</p>}

      <p className="section-title">Проверка</p>
      <form onSubmit={review}>
        <div className="field field-row">
          <label style={{ flex: 1 }}>Статус
            <select value={status} onChange={(e) => setStatus(e.target.value)}>
              <option value="reviewed">reviewed</option>
              <option value="needs_fix">needs_fix</option>
              <option value="accepted">accepted</option>
            </select>
          </label>
          <button className="primary" type="submit" disabled={busy} style={{ alignSelf: "flex-end" }}>Проверить</button>
        </div>
        <div className="field"><label>Комментарий<input placeholder="комментарий" value={comment} onChange={(e) => setComment(e.target.value)} /></label></div>
      </form>
      <button className="small" onClick={addComment} disabled={busy || !comment.trim()}>Добавить комментарий</button>

      {(d?.comments ?? []).length > 0 && <p className="section-title">Комментарии</p>}
      {(d?.comments ?? []).map((c) => (
        <div className="row" key={c.id}><span className="muted">{c.text}</span></div>
      ))}
    </div>
  );
}

function FinanceCard({
  receipts,
  students,
  chargeRefresh,
  onChanged,
}: {
  receipts: Async<Receipt[]>;
  students: StudentLink[];
  chargeRefresh: { studentId: string; seq: number } | null;
  onChanged: () => void;
}) {
  const [error, setError] = useState<string | null>(null);
  const [balStudent, setBalStudent] = useState("");
  const [balance, setBalance] = useState<Balance | null>(null);
  const [txns, setTxns] = useState<Transaction[]>([]);
  const [balanceNotice, setBalanceNotice] = useState<string | null>(null);
  const [actingId, setActingId] = useState<string | null>(null);
  const [corrAmount, setCorrAmount] = useState("");
  const [corrComment, setCorrComment] = useState("");
  const [corrBusy, setCorrBusy] = useState(false);
  const [corrNotice, setCorrNotice] = useState<string | null>(null);

  function nameOf(id: string) {
    return students.find((s) => s.student_id === id)?.display_name ?? id.slice(0, 8);
  }

  async function loadJournal(id: string) {
    if (!id) { setTxns([]); return; }
    try {
      setTxns(await api.get<Transaction[]>(`/students/${id}/transactions`));
    } catch (err) {
      setError((err as Error).message);
    }
  }

  async function submitCorrection(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setCorrNotice(null);
    if (!balStudent) { setError("Выберите ученика"); return; }
    setCorrBusy(true);
    try {
      await api.post(`/students/${balStudent}/corrections`, {
        amount: Number(corrAmount),
        comment: corrComment,
      });
      setCorrNotice("Коррекция применена.");
      setCorrAmount(""); setCorrComment("");
      setBalance(await api.get<Balance>(`/students/${balStudent}/balance`));
      loadJournal(balStudent);
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setCorrBusy(false);
    }
  }

  async function act(id: string, path: string, body?: unknown) {
    setError(null);
    setActingId(id);
    try {
      await api.post(path, body);
      receipts.reload();
      if (balStudent) loadBalance(balStudent);
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setActingId(null);
    }
  }

  function reject(r: Receipt) {
    if (!window.confirm(`Отклонить чек ученика ${nameOf(r.student_id)} на ${Math.round(r.amount)} ₽?`)) return;
    const reason = window.prompt("Причина отклонения (необязательно):", "") ?? "";
    act(r.id, `/payments/receipts/${r.id}/reject`, { comment: reason });
  }

  async function loadBalance(id: string) {
    setBalStudent(id);
    setBalanceNotice(null);
    setCorrNotice(null);
    setBalance(null);
    setTxns([]);
    if (!id) return;
    try {
      setBalance(await api.get<Balance>(`/students/${id}/balance`));
      loadJournal(id);
    } catch (err) {
      setError((err as Error).message);
    }
  }

  useEffect(() => {
    if (!chargeRefresh) return;

    const refresh = chargeRefresh;
    let cancelled = false;
    const initialBalance =
      balStudent === refresh.studentId ? balance?.balance : undefined;

    async function pollBalance() {
      setBalStudent(refresh.studentId);
      setBalanceNotice("Ждём начисление по завершённому занятию…");
      const deadline = Date.now() + 10000;
      while (!cancelled) {
        try {
          const next = await api.get<Balance>(`/students/${refresh.studentId}/balance`);
          if (cancelled) return;
          setBalance(next);
          if (initialBalance === undefined || next.balance !== initialBalance || Date.now() >= deadline) {
            setBalanceNotice(null);
            return;
          }
        } catch (err) {
          if (!cancelled) {
            setError((err as Error).message);
            setBalanceNotice(null);
          }
          return;
        }
        await new Promise((resolve) => setTimeout(resolve, 1000));
      }
    }

    pollBalance();
    return () => {
      cancelled = true;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [chargeRefresh?.seq]);

  return (
    <Card title="Чеки на проверку" icon="receipt_long" id="finance">
      <ErrorMsg error={error} />
      {(receipts.data ?? []).map((r) => (
        <div className="row" key={r.id}>
          <span>{nameOf(r.student_id)} · {Math.round(r.amount)} ₽</span>
          <span className="btn-group">
            <button className="small" onClick={() => openFile(r.file_id).catch((e) => setError((e as Error).message))}>Чек</button>
            <button className="small" disabled={actingId === r.id} onClick={() => act(r.id, `/payments/receipts/${r.id}/confirm`)}>Подтвердить</button>
            <button className="small" disabled={actingId === r.id} onClick={() => reject(r)}>Отклонить</button>
          </span>
        </div>
      ))}
      <ListState query={receipts} empty="Чеков на проверку нет." />

      <div style={{ marginTop: 12, borderTop: "0.5px solid var(--border)", paddingTop: 12 }}>
        <p className="section-title">Долг ученика</p>
        <label>Ученик
          <select value={balStudent} onChange={(e) => loadBalance(e.target.value)}>
            <option value="">— ученик —</option>
            {students.map((s) => <option key={s.id} value={s.student_id}>{s.display_name}</option>)}
          </select>
        </label>
        {balance && (
          <p style={{ marginTop: 8 }}>
            Долг: <strong>{Math.round(balance.balance)} {balance.currency}</strong>
          </p>
        )}
        {balanceNotice && <p className="hint">{balanceNotice}</p>}

        {balStudent && (
          <>
            <p className="section-title" style={{ marginTop: 10 }}>Журнал операций</p>
            {txns.map((t) => (
              <div className="row" key={t.id}>
                <span className="muted">{t.type}{t.comment ? ` · ${t.comment}` : ""}</span>
                <span>{t.amount > 0 ? "+" : ""}{Math.round(t.amount)} {t.currency}</span>
              </div>
            ))}
            {txns.length === 0 && <p className="hint">Операций пока нет.</p>}

            <form onSubmit={submitCorrection} style={{ marginTop: 10 }}>
              <p className="section-title">Скорректировать баланс</p>
              <Notice text={corrNotice} />
              <div className="field field-row">
                <label style={{ width: 130 }}>Сумма ± ₽<input type="number" placeholder="напр. -500" value={corrAmount} onChange={(e) => setCorrAmount(e.target.value)} required /></label>
                <label style={{ flex: 1 }}>Комментарий<input placeholder="причина" value={corrComment} onChange={(e) => setCorrComment(e.target.value)} required /></label>
              </div>
              <button className="primary small" type="submit" disabled={corrBusy}>{corrBusy ? "Применение…" : "Применить коррекцию"}</button>
            </form>
          </>
        )}
      </div>
    </Card>
  );
}
