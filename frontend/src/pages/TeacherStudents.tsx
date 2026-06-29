import { useMemo, useState } from "react";
import { Link } from "react-router-dom";
import { ApiError, api, reports, type StudentLink, type TeacherDashboard } from "../api";
import {
  AppShell,
  Avatar,
  Button,
  Card,
  EmptyState,
  ErrorMsg,
  Field,
  Icon,
  Modal,
  Segmented,
  SkeletonRows,
  StatusPill,
  useAsync,
  useToast,
  type TabItem,
} from "../ui";
import { money, teacherNav } from "./teacherNav";

function signedMoney(value?: number, currency = "RUB"): string {
  if (typeof value !== "number") return "—";
  if (value === 0) return `0 ${currency}`;
  return `${value < 0 ? "−" : ""}${Math.abs(Math.round(value)).toLocaleString("ru-RU")} ${currency}`;
}

function studentTone(balance?: number): "teacher" | "student" | "muted" {
  if (typeof balance !== "number" || balance === 0) return "muted";
  return balance < 0 ? "student" : "teacher";
}

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
    allStudents.forEach((s) => { base[s.status] = (base[s.status] ?? 0) + 1; });
    return base;
  }, [allStudents]);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return allStudents.filter((s) => {
      const matchesQuery = !q || s.display_name.toLowerCase().includes(q) || (s.subject ?? "").toLowerCase().includes(q) || (s.goal ?? "").toLowerCase().includes(q);
      const matchesStatus = status === "all" || s.status === status;
      return matchesQuery && matchesStatus;
    });
  }, [allStudents, query, status]);

  const segments: TabItem[] = [
    { key: "all", label: "Все", count: counts.all },
    { key: "active", label: "Активные", count: counts.active },
    { key: "invited", label: "Приглашены", count: counts.invited },
    { key: "archived", label: "Архив", count: counts.archived },
  ];

  function balanceOf(studentId: string): number | undefined {
    return summaries.find((s) => s.student_id === studentId)?.finance.balance_amount;
  }

  return (
    <AppShell
      title="Ученики"
      subtitle={`${counts.all} учеников · общий долг ${money(dashboard.data?.total_debt_amount)}`}
      navSection="Работа"
      navItems={teacherNav("students", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={<Button variant="primary" icon="person_add" onClick={() => setCreateOpen(true)}>Новый ученик</Button>}
    >
      <div className="container">
        <div className="teacher-toolbar">
          <Segmented items={segments} active={status} onChange={setStatus} />
          <div className="toolbar-right">
            <div className="search-field">
              <Icon name="search" />
              <input placeholder="Поиск по имени или предмету…" value={query} onChange={(e) => setQuery(e.target.value)} />
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

        {students.loading && !students.data ? (
          <Card title="Ученики" icon="group"><SkeletonRows count={5} /></Card>
        ) : students.error ? (
          <ErrorMsg error={students.error} />
        ) : filtered.length === 0 ? (
          <EmptyState icon="person_search" title="Никого не найдено" hint="Измените запрос или фильтр." />
        ) : view === "grid" ? (
          <div className="student-card-grid">
            {filtered.map((s) => {
              const balance = balanceOf(s.student_id);
              const summary = summaries.find((x) => x.student_id === s.student_id);
              return (
                <Link className="student-card" to={`/teacher/students/${s.student_id}`} key={s.id}>
                  <div className="student-card-top">
                    <Avatar name={s.display_name} tone={studentTone(balance)} />
                    <StatusPill status={s.status} />
                  </div>
                  <div className="student-card-name">{s.display_name}</div>
                  <div className="muted">{s.subject || "Предмет не указан"}</div>
                  <div className="student-card-stats">
                    <span><strong className={balance && balance < 0 ? "finance-credit" : ""}>{signedMoney(balance, summary?.finance.currency)}</strong><em>{balance && balance < 0 ? "переплата" : "долг"}</em></span>
                    <span><strong>{summary?.activity.upcoming_lessons_count ?? 0}</strong><em>занятия</em></span>
                  </div>
                  <div className="student-card-footer">
                    <span>{s.goal || "Цель не указана"}</span>
                    <Icon name="chevron_right" />
                  </div>
                </Link>
              );
            })}
          </div>
        ) : (
          <Card title="Ученики" icon="group">
            {filtered.map((s) => {
              const balance = balanceOf(s.student_id);
              const summary = summaries.find((x) => x.student_id === s.student_id);
              return (
                <Link className="resource-row" to={`/teacher/students/${s.student_id}`} key={s.id} style={{ textDecoration: "none", color: "inherit" }}>
                  <Avatar name={s.display_name} tone={studentTone(balance)} />
                  <div className="resource-main">
                    <div className="summary-title">{s.display_name}</div>
                    <div className="summary-grid">
                      <span>{s.subject || "Предмет не указан"}</span>
                      <span>Ставка: {money(s.hourly_rate)}</span>
                      <span>{balance && balance < 0 ? "Переплата" : "Долг"}: {signedMoney(balance, summary?.finance.currency)}</span>
                      <span>Ближайшие занятия: {summary?.activity.upcoming_lessons_count ?? 0}</span>
                    </div>
                  </div>
                  <StatusPill status={s.status} />
                  <Icon name="chevron_right" />
                </Link>
              );
            })}
          </Card>
        )}
      </div>

      {createOpen && (
        <CreateStudentModal
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

function generatePassword(): string {
  const chars = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
  let pw = "";
  for (let i = 0; i < 8; i++) pw += chars[Math.floor(Math.random() * chars.length)];
  return pw;
}

function CreateStudentModal({ onClose, onCreated }: { onClose: () => void; onCreated: () => void }) {
  const toast = useToast();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState(() => generatePassword());
  const [name, setName] = useState("");
  const [subject, setSubject] = useState("");
  const [rate, setRate] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [created, setCreated] = useState(false);

  async function submit() {
    setError(null);
    setBusy(true);
    try {
      await api.post("/students", {
        email,
        password,
        display_name: name,
        subject: subject || undefined,
        hourly_rate: rate ? Number(rate) : undefined,
      });
      setCreated(true);
      toast({ tone: "success", title: "Ученик создан", body: `Передайте «${name}» данные для входа.` });
      onCreated();
    } catch (err) {
      if (err instanceof ApiError && err.status === 409) {
        setError("Этот email уже зарегистрирован. Используйте другой адрес.");
      } else {
        setError((err as Error).message);
      }
      toast({ tone: "danger", title: "Не удалось создать", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  if (created) {
    return (
      <Modal title="Аккаунт создан" subtitle="Передайте ученику данные для первого входа" onClose={onClose}
        footer={
          <>
            <Button onClick={() => copyText(`${email}\n${password}`, toast)} icon="content_copy">Скопировать</Button>
            <Button variant="primary" onClick={onClose}>Готово</Button>
          </>
        }
      >
        <div className="created-credentials">
          <div className="created-icon"><Icon name="check_circle" /></div>
          <p className="muted">Пароль временный — ученик сменит его при первом входе.</p>
          <CredentialRow icon="mail" label="Email" value={email} toast={toast} />
          <CredentialRow icon="key" label="Временный пароль" value={password} strong toast={toast} />
        </div>
      </Modal>
    );
  }

  return (
    <Modal
      title="Новый ученик"
      subtitle="Создайте аккаунт и временный пароль"
      onClose={onClose}
      onSubmit={submit}
      footer={
        <>
          <Button type="button" onClick={onClose}>Отмена</Button>
          <Button variant="primary" type="submit" icon="person_add" loading={busy} disabled={!name.trim() || !email.trim()}>Создать</Button>
        </>
      }
    >
      <ErrorMsg error={error} />
      <div className="modal-fields">
        <Field label="Имя и фамилия">
          <input value={name} onChange={(e) => setName(e.target.value)} required placeholder="Например: Лиза Орлова" />
        </Field>
        <Field label="Email ученика">
          <input type="email" value={email} onChange={(e) => setEmail(e.target.value)} required placeholder="student@example.ru" />
        </Field>
        <div className="field-row modal-field-row">
          <Field label="Предмет">
            <input value={subject} onChange={(e) => setSubject(e.target.value)} placeholder="Математика" />
          </Field>
          <Field label="Ставка ₽" className="time-field">
            <input type="number" min="0" value={rate} onChange={(e) => setRate(e.target.value)} placeholder="1000" />
          </Field>
        </div>
        <Field label="Временный пароль" hint="Ученик сменит его при первом входе.">
          <div className="generated-field">
            <input value={password} onChange={(e) => setPassword(e.target.value)} minLength={8} required />
            <button type="button" onClick={() => setPassword(generatePassword())} title="Сгенерировать заново">
              <Icon name="autorenew" />
            </button>
          </div>
        </Field>
      </div>
    </Modal>
  );
}

function CredentialRow({ icon, label, value, strong = false, toast }: { icon: string; label: string; value: string; strong?: boolean; toast: ReturnType<typeof useToast> }) {
  return (
    <div className="credential-row">
      <Icon name={icon} />
      <div>
        <span>{label}</span>
        <strong className={strong ? "credential-strong" : ""}>{value}</strong>
      </div>
      <button type="button" className="icon-button compact" onClick={() => copyText(value, toast)} title="Скопировать">
        <Icon name="content_copy" />
      </button>
    </div>
  );
}

function copyText(value: string, toast: ReturnType<typeof useToast>) {
  navigator.clipboard?.writeText(value).then(
    () => toast({ tone: "success", title: "Скопировано", body: "Данные в буфере обмена" }),
    () => toast({ tone: "danger", title: "Не удалось скопировать", body: "Скопируйте вручную" }),
  );
}
