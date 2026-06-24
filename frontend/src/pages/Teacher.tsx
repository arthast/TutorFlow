import { useEffect, useState, type FormEvent } from "react";
import {
  api,
  openFile,
  type Assignment,
  type AssignmentDetail,
  type Balance,
  type CompleteLessonResponse,
  type FileMeta,
  type Lesson,
  type Receipt,
  type StudentLink,
} from "../api";
import { Card, ErrorMsg, FileChips, ListState, Notice, NotificationsCard, StatusPill, TopBar, fmtDate, useAsync } from "../ui";

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

  return (
    <>
      <TopBar />
      <div className="container">
        <div className="metrics">
          <Metric label="Ученики" value={studentList.length} />
          <Metric label="Занятия (запланировано)" value={scheduled} />
          <Metric label="Чеки на проверку" value={(receipts.data ?? []).length} />
        </div>

        <div className="grid">
          <NotificationsCard />
          <StudentsCard students={students} />
          <LessonsCard
            lessons={lessons}
            students={studentList}
            onChargePending={(studentId) => setChargeRefresh({ studentId, seq: Date.now() })}
          />
          <AssignmentsCard assignments={assignments} students={studentList} />
          <FinanceCard receipts={receipts} students={studentList} chargeRefresh={chargeRefresh} />
        </div>
      </div>
    </>
  );
}

function Metric({ label, value }: { label: string; value: number }) {
  return (
    <div className="metric">
      <div className="label">{label}</div>
      <div className="value">{value}</div>
    </div>
  );
}

type Async<T> = ReturnType<typeof useAsync<T>>;

function StudentsCard({ students }: { students: Async<StudentLink[]> }) {
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
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Ученики">
      {(students.data ?? []).map((s) => (
        <div className="row" key={s.id}>
          <span>{s.display_name}{s.subject ? ` · ${s.subject}` : ""}</span>
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
}: {
  lessons: Async<Lesson[]>;
  students: StudentLink[];
  onChargePending: (studentId: string) => void;
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

  function nameOf(id: string) {
    return students.find((s) => s.student_id === id)?.display_name ?? id.slice(0, 8);
  }

  async function complete(id: string) {
    setError(null);
    setNotice(null);
    setActingId(id);
    try {
      const result = await api.post<CompleteLessonResponse>(`/lessons/${id}/complete`);
      lessons.reload();
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
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Занятия">
      {(lessons.data ?? []).map((l) => (
        <div key={l.id}>
          <div className="row">
            <span>{nameOf(l.student_id)} · {fmtDate(l.starts_at)}{l.topic ? ` · ${l.topic}` : ""}</span>
            {l.status === "scheduled" ? (
              <span className="btn-group">
                <button className="small" disabled={actingId === l.id} onClick={() => complete(l.id)}>
                  {actingId === l.id ? "…" : "Завершить"}
                </button>
                <button className="small" disabled={actingId === l.id} onClick={() => cancel(l.id)}>Отмена</button>
              </span>
            ) : (
              <StatusPill status={l.status} />
            )}
          </div>
          <FileChips fileIds={l.file_ids} label="Материалы урока" />
        </div>
      ))}
      <ListState query={lessons} empty="Занятий пока нет." />
      <Notice text={notice} />

      <form onSubmit={create} style={{ marginTop: 12, borderTop: "0.5px solid var(--border)", paddingTop: 12 }}>
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

function AssignmentsCard({ assignments, students }: { assignments: Async<Assignment[]>; students: StudentLink[] }) {
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
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Домашние задания">
      {(assignments.data ?? []).map((a) => (
        <div key={a.id}>
          <div className="row">
            <button className="small" onClick={() => setOpenId(openId === a.id ? null : a.id)} style={{ flex: 1, textAlign: "left" }}>
              {a.title}
            </button>
            <StatusPill status={a.status} />
          </div>
          {openId === a.id && <AssignmentDetailView id={a.id} onChange={() => assignments.reload()} />}
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
}: {
  receipts: Async<Receipt[]>;
  students: StudentLink[];
  chargeRefresh: { studentId: string; seq: number } | null;
}) {
  const [error, setError] = useState<string | null>(null);
  const [balStudent, setBalStudent] = useState("");
  const [balance, setBalance] = useState<Balance | null>(null);
  const [balanceNotice, setBalanceNotice] = useState<string | null>(null);
  const [actingId, setActingId] = useState<string | null>(null);

  function nameOf(id: string) {
    return students.find((s) => s.student_id === id)?.display_name ?? id.slice(0, 8);
  }

  async function act(id: string, path: string, body?: unknown) {
    setError(null);
    setActingId(id);
    try {
      await api.post(path, body);
      receipts.reload();
      if (balStudent) loadBalance(balStudent);
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
    setBalance(null);
    if (!id) return;
    try {
      setBalance(await api.get<Balance>(`/students/${id}/balance`));
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
    <Card title="Чеки на проверку">
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
      </div>
    </Card>
  );
}
