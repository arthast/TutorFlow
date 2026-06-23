import { useMemo, useState, type FormEvent } from "react";
import {
  api,
  type Assignment,
  type AssignmentDetail,
  type Balance,
  type FileMeta,
  type Lesson,
  type Receipt,
} from "../api";
import { useAuth } from "../auth";
import { Card, ErrorMsg, StatusPill, TopBar, fmtDate, useAsync } from "../ui";

const TO_SUBMIT = new Set(["assigned", "needs_fix"]);

export default function Student() {
  const { user } = useAuth();
  const studentId = user?.user_id ?? "";
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);
  const balance = useAsync<Balance | null>(
    () => (studentId ? api.get<Balance>(`/students/${studentId}/balance`) : Promise.resolve(null)),
    [studentId],
  );

  // teacher_id для чека берём из занятий/ДЗ ученика (баланса у ученика нет).
  const teacherIds = useMemo(() => {
    const ids = new Set<string>();
    (lessons.data ?? []).forEach((l) => ids.add(l.teacher_id));
    (assignments.data ?? []).forEach((a) => ids.add(a.teacher_id));
    return [...ids];
  }, [lessons.data, assignments.data]);

  const toSubmit = (assignments.data ?? []).filter((a) => TO_SUBMIT.has(a.status)).length;

  return (
    <>
      <TopBar />
      <div className="container">
        <div className="metrics">
          <Metric label="Итого долг" value={balance.data ? `${Math.round(balance.data.balance)} ${balance.data.currency}` : "—"} />
          <Metric label="Занятия" value={(lessons.data ?? []).length} />
          <Metric label="ДЗ к сдаче" value={toSubmit} />
          <Metric label="Мои чеки" value={(receipts.data ?? []).length} />
        </div>

        <div className="grid">
          <AssignmentsCard assignments={assignments} />
          <ReceiptCard teacherIds={teacherIds} onSent={() => receipts.reload()} />
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

function LessonsCard({ lessons }: { lessons: Async<Lesson[]> }) {
  return (
    <Card title="Мои занятия">
      {(lessons.data ?? []).map((l) => (
        <div className="row" key={l.id}>
          <span>{l.topic || "Занятие"} · {fmtDate(l.starts_at)}</span>
          <span className="btn-group" style={{ alignItems: "center" }}>
            {typeof l.price === "number" && <span className="muted">{Math.round(l.price)} ₽</span>}
            <StatusPill status={l.status} />
          </span>
        </div>
      ))}
      {lessons.data?.length === 0 && <p className="hint">Занятий пока нет.</p>}
    </Card>
  );
}

function AssignmentsCard({ assignments }: { assignments: Async<Assignment[]> }) {
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
          {openId === a.id && <SubmitView id={a.id} onChange={() => assignments.reload()} />}
        </div>
      ))}
      {assignments.data?.length === 0 && <p className="hint">Заданий пока нет.</p>}
    </Card>
  );
}

function SubmitView({ id, onChange }: { id: string; onChange: () => void }) {
  const detail = useAsync<AssignmentDetail>(() => api.get(`/assignments/${id}`), [id]);
  const [text, setText] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setBusy(true);
    try {
      const fileIds: string[] = [];
      if (file) {
        const form = new FormData();
        form.append("file", file);
        form.append("purpose", "submission_file");
        const meta = await api.upload<FileMeta>("/files", form);
        fileIds.push(meta.id);
      }
      await api.post(`/assignments/${id}/submit`, {
        text_answer: text || undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      setText(""); setFile(null);
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
      {d?.description && <p className="muted">{d.description}</p>}
      {(d?.comments ?? []).map((c) => (
        <div className="row" key={c.id}><span className="muted">Комментарий: {c.text}</span></div>
      ))}
      <form onSubmit={submit}>
        <div className="field"><textarea placeholder="Текст решения" value={text} onChange={(e) => setText(e.target.value)} /></div>
        <div className="field"><input type="file" onChange={(e) => setFile(e.target.files?.[0] ?? null)} /></div>
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
  const [ok, setOk] = useState(false);
  const [busy, setBusy] = useState(false);

  const effectiveTeacher = teacherId || (teacherIds.length === 1 ? teacherIds[0] : "");

  async function send(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setOk(false);
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
      setAmount(""); setFile(null); setOk(true);
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
      {ok && <p className="muted">Чек отправлен — ждёт подтверждения преподавателя.</p>}
      <form onSubmit={send}>
        {teacherIds.length > 1 && (
          <div className="field">
            <label>Преподаватель</label>
            <select value={teacherId} onChange={(e) => setTeacherId(e.target.value)} required>
              <option value="">— выбрать —</option>
              {teacherIds.map((id) => <option key={id} value={id}>{id.slice(0, 8)}…</option>)}
            </select>
          </div>
        )}
        <div className="field"><input type="number" placeholder="сумма ₽" value={amount} onChange={(e) => setAmount(e.target.value)} required /></div>
        <div className="field"><input type="file" onChange={(e) => setFile(e.target.files?.[0] ?? null)} required /></div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Отправка…" : "Отправить чек"}</button>
      </form>
      <p className="hint">Это заявка об оплате: баланс/долг ведёт преподаватель и подтверждает чек вручную.</p>
    </Card>
  );
}

function ReceiptsListCard({ receipts }: { receipts: Async<Receipt[]> }) {
  return (
    <Card title="Мои чеки">
      {(receipts.data ?? []).map((r) => (
        <div className="row" key={r.id}>
          <span>{Math.round(r.amount)} ₽ · {fmtDate(r.submitted_at)}</span>
          <StatusPill status={r.status} />
        </div>
      ))}
      {receipts.data?.length === 0 && <p className="hint">Чеков пока нет.</p>}
    </Card>
  );
}

function PasswordCard() {
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [ok, setOk] = useState(false);

  async function change(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setOk(false);
    try {
      await api.post("/auth/change-password", { current_password: current, new_password: next });
      setCurrent(""); setNext(""); setOk(true);
    } catch (err) {
      setError((err as Error).message);
    }
  }

  return (
    <Card title="Сменить пароль">
      <ErrorMsg error={error} />
      {ok && <p className="muted">Пароль обновлён.</p>}
      <form onSubmit={change}>
        <div className="field"><input type="password" placeholder="текущий пароль" value={current} onChange={(e) => setCurrent(e.target.value)} required /></div>
        <div className="field"><input type="password" placeholder="новый пароль (мин. 8)" value={next} onChange={(e) => setNext(e.target.value)} minLength={8} required /></div>
        <button className="primary" type="submit">Обновить</button>
      </form>
    </Card>
  );
}
