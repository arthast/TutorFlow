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
  const [view, setView] = useState<"grid" | "list">("grid");
  const [createOpen, setCreateOpen] = useState(false);

  const summaries = dashboard.data?.students ?? [];
  const allStudents = students.data ?? [];
  const counts = useMemo(() => {
    const base: Record<string, number> = { all: allStudents.length, active: 0, invited: 0, archived: 0 };
    allStudents.forEach((student) => {
      base[student.status] = (base[student.status] ?? 0) + 1;
    });
    return base;
  }, [allStudents]);
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return allStudents.filter((student) => {
      const matchesQuery =
        !q ||
        student.display_name.toLowerCase().includes(q) ||
        (student.subject ?? "").toLowerCase().includes(q) ||
        (student.goal ?? "").toLowerCase().includes(q);
      const matchesStatus = status === "all" || student.status === status;
      return matchesQuery && matchesStatus;
    });
  }, [allStudents, query, status]);

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
                {item === "all" ? "Все" : item === "active" ? "Активные" : item === "invited" ? "Приглашены" : "Архив"}
                <span className="tab-count">{counts[item] ?? 0}</span>
              </button>
            ))}
          </div>
          <div className="toolbar-right">
            <div className="search-field">
              <Icon name="search" />
              <input placeholder="Поиск по имени или предмету..." value={query} onChange={(event) => setQuery(event.target.value)} />
            </div>
            <div className="view-toggle" aria-label="Вид списка учеников">
              <button className={view === "grid" ? "active" : ""} type="button" onClick={() => setView("grid")} title="Карточки">
                <Icon name="grid_view" />
              </button>
              <button className={view === "list" ? "active" : ""} type="button" onClick={() => setView("list")} title="Список">
                <Icon name="view_list" />
              </button>
            </div>
          </div>
        </div>

        <Card title="Ученики" icon="group">
          {view === "grid" && filtered.length > 0 && (
            <div className="student-card-grid">
              {filtered.map((student) => {
                const summary = summaries.find((item) => item.student_id === student.student_id);
                return (
                  <Link className="student-card" to={`/teacher/students/${student.student_id}`} key={student.id}>
                    <div className="student-card-top">
                      <div className="avatar">{student.display_name.slice(0, 2).toUpperCase()}</div>
                      <StatusPill status={student.status} />
                    </div>
                    <div className="student-card-name">{student.display_name}</div>
                    <div className="muted">{student.subject || "Предмет не указан"}</div>
                    <div className="student-card-stats">
                      <span><strong>{money(summary?.finance.debt_amount, summary?.finance.currency)}</strong><em>долг</em></span>
                      <span><strong>{summary?.activity.upcoming_lessons_count ?? 0}</strong><em>занятия</em></span>
                    </div>
                    <div className="student-card-footer">
                      <span>{student.goal || "Цель не указана"}</span>
                      <Icon name="chevron_right" />
                    </div>
                  </Link>
                );
              })}
            </div>
          )}
          {view === "list" && filtered.map((student) => {
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
  const [password, setPassword] = useState(() => generatePassword());
  const [name, setName] = useState("");
  const [subject, setSubject] = useState("");
  const [rate, setRate] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [created, setCreated] = useState(false);

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
      setCreated(true);
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
        {!created ? (
          <>
            <div className="modal-fields">
              <div className="field"><label>Имя и фамилия<input value={name} onChange={(event) => setName(event.target.value)} required placeholder="Например: Лиза Орлова" /></label></div>
              <div className="field"><label>Email ученика<input type="email" value={email} onChange={(event) => setEmail(event.target.value)} required placeholder="student@example.ru" /></label></div>
              <div className="field-row modal-field-row">
                <label>Предмет<input value={subject} onChange={(event) => setSubject(event.target.value)} placeholder="Математика" /></label>
                <label>Ставка ₽<input type="number" min="0" value={rate} onChange={(event) => setRate(event.target.value)} /></label>
              </div>
              <div className="field">
                <label>Временный пароль
                  <div className="generated-field">
                    <input value={password} onChange={(event) => setPassword(event.target.value)} minLength={8} required />
                    <button type="button" onClick={() => setPassword(generatePassword())} title="Сгенерировать заново">
                      <Icon name="autorenew" />
                    </button>
                  </div>
                </label>
                <p className="hint">Ученик сменит его при первом входе.</p>
              </div>
            </div>
            <div className="modal-actions">
              <button type="button" onClick={onClose}>Отмена</button>
              <button className="primary" type="submit" disabled={busy || students.loading || !name.trim() || !email.trim()}>
                <Icon name="person_add" />
                {busy ? "Создание..." : "Создать"}
              </button>
            </div>
          </>
        ) : (
          <div className="created-credentials">
            <div className="created-icon"><Icon name="check_circle" /></div>
            <h3>Аккаунт создан</h3>
            <p className="muted">Передайте ученику данные для первого входа.</p>
            <CredentialRow icon="mail" label="Email" value={email} />
            <CredentialRow icon="key" label="Временный пароль" value={password} strong />
            <div className="modal-actions modal-actions-wide">
              <button type="button" onClick={() => copyText(`${email}\n${password}`, setNotice)}>
                <Icon name="content_copy" />Скопировать
              </button>
              <button className="primary" type="button" onClick={onClose}>Готово</button>
            </div>
          </div>
        )}
      </form>
    </div>
  );
}

function generatePassword(): string {
  return "TF-" + Math.random().toString(36).slice(2, 6).toUpperCase() + "-" + Math.random().toString(36).slice(2, 6).toUpperCase();
}

function CredentialRow({ icon, label, value, strong = false }: { icon: string; label: string; value: string; strong?: boolean }) {
  const [copied, setCopied] = useState(false);
  return (
    <div className="credential-row">
      <Icon name={icon} />
      <div>
        <span>{label}</span>
        <strong className={strong ? "credential-strong" : ""}>{value}</strong>
      </div>
      <button type="button" className="icon-button compact" onClick={() => copyText(value, () => setCopied(true))} title="Скопировать">
        <Icon name={copied ? "done" : "content_copy"} />
      </button>
    </div>
  );
}

function copyText(value: string, onCopied: (message: string) => void) {
  navigator.clipboard?.writeText(value).then(
    () => onCopied("Скопировано."),
    () => onCopied("Не удалось скопировать."),
  );
}
