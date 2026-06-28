import { useMemo, useState, type FormEvent } from "react";
import { Link } from "react-router-dom";
import { api, reports, type StudentLink, type TeacherDashboard } from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, Notice, StatusPill, useAsync } from "../ui";
import { money, teacherNav } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;

export default function TeacherStudents() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const [query, setQuery] = useState("");
  const [status, setStatus] = useState("all");
  const [createOpen, setCreateOpen] = useState(false);

  const summaries = dashboard.data?.students ?? [];
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return (students.data ?? []).filter((student) => {
      const matchesQuery =
        !q ||
        student.display_name.toLowerCase().includes(q) ||
        (student.subject ?? "").toLowerCase().includes(q) ||
        (student.goal ?? "").toLowerCase().includes(q);
      const matchesStatus = status === "all" || student.status === status;
      return matchesQuery && matchesStatus;
    });
  }, [query, status, students.data]);

  return (
    <AppShell
      title="Ученики"
      subtitle="Список, статусы и быстрый переход в карточку"
      navSection="Работа"
      navItems={teacherNav("students", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={
        <button className="primary-action" type="button" onClick={() => setCreateOpen(true)}>
          <Icon name="person_add" />
          <span>Новый ученик</span>
        </button>
      }
    >
      <div className="container">
        <div className="teacher-toolbar">
          <div className="segmented">
            {["all", "active", "invited", "archived"].map((item) => (
              <button className={status === item ? "active" : ""} key={item} onClick={() => setStatus(item)}>
                {item === "all" ? "Все" : item}
              </button>
            ))}
          </div>
          <div className="search-field">
            <Icon name="search" />
            <input placeholder="Поиск по имени, предмету или цели..." value={query} onChange={(event) => setQuery(event.target.value)} />
          </div>
        </div>

        <Card title="Ученики" icon="group">
          {filtered.map((student) => {
            const summary = summaries.find((item) => item.student_id === student.student_id);
            return (
              <div className="resource-row" key={student.id}>
                <div className="avatar">{student.display_name.slice(0, 2).toUpperCase()}</div>
                <div className="resource-main">
                  <Link className="summary-title" to={`/teacher/students/${student.student_id}`}>{student.display_name}</Link>
                  <div className="summary-grid">
                    <span>{student.subject || "Предмет не указан"}</span>
                    <span>{student.goal || "Цель не указана"}</span>
                    <span>Ставка: {money(student.hourly_rate)}</span>
                    <span>Долг: {money(summary?.finance.debt_amount, summary?.finance.currency)}</span>
                    <span>Ближайшие занятия: {summary?.activity.upcoming_lessons_count ?? 0}</span>
                  </div>
                </div>
                <StatusPill status={student.status} />
              </div>
            );
          })}
          <ListState query={{ ...students, data: filtered }} empty="Ученики не найдены." />
        </Card>
      </div>

      {createOpen && (
        <CreateStudentModal
          students={students}
          onClose={() => setCreateOpen(false)}
          onCreated={() => {
            students.reload();
            dashboard.reload();
            setCreateOpen(false);
          }}
        />
      )}
    </AppShell>
  );
}

function CreateStudentModal({
  students,
  onClose,
  onCreated,
}: {
  students: Async<StudentLink[]>;
  onClose: () => void;
  onCreated: () => void;
}) {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [name, setName] = useState("");
  const [subject, setSubject] = useState("");
  const [rate, setRate] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function create(event: FormEvent) {
    event.preventDefault();
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
      setNotice(`Ученик "${name}" создан.`);
      onCreated();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="modal-overlay" onMouseDown={onClose}>
      <form className="modal-panel" onMouseDown={(event) => event.stopPropagation()} onSubmit={create}>
        <div className="modal-heading">
          <div>
            <h2>Новый ученик</h2>
            <p>Аккаунт создаётся с временным паролем</p>
          </div>
          <button className="icon-button" type="button" onClick={onClose} title="Закрыть">
            <Icon name="close" />
          </button>
        </div>
        <ErrorMsg error={error} />
        <Notice text={notice} />
        <div className="modal-fields">
          <div className="field"><label>Email<input type="email" value={email} onChange={(event) => setEmail(event.target.value)} required /></label></div>
          <div className="field"><label>Временный пароль<input value={password} onChange={(event) => setPassword(event.target.value)} minLength={8} required /></label></div>
          <div className="field"><label>Имя<input value={name} onChange={(event) => setName(event.target.value)} required /></label></div>
          <div className="field-row modal-field-row">
            <label>Предмет<input value={subject} onChange={(event) => setSubject(event.target.value)} /></label>
            <label>Ставка ₽<input type="number" min="0" value={rate} onChange={(event) => setRate(event.target.value)} /></label>
          </div>
        </div>
        <div className="modal-actions">
          <button type="button" onClick={onClose}>Отмена</button>
          <button className="primary" type="submit" disabled={busy || students.loading}>
            <Icon name="person_add" />
            {busy ? "Создание..." : "Создать"}
          </button>
        </div>
      </form>
    </div>
  );
}
