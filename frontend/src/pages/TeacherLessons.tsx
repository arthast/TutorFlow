import { useMemo, useState, type FormEvent } from "react";
import {
  api,
  type CompleteLessonResponse,
  type Lesson,
  type StudentLink,
  type TeacherDashboard,
} from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, Notice, StatusPill, fmtDate, useAsync } from "../ui";
import { initials, teacherNav, timeOnly } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;

function toIso(local: string): string {
  return new Date(local).toISOString();
}

function isoToLocalInput(iso: string): string {
  const date = new Date(iso);
  if (isNaN(date.getTime())) return "";
  const offset = date.getTimezoneOffset();
  const local = new Date(date.getTime() - offset * 60000);
  return local.toISOString().slice(0, 16);
}

function toIsoFromParts(date: string, time: string): string {
  return new Date(`${date}T${time}`).toISOString();
}

function addMinutesIso(date: string, time: string, minutes: number): string {
  const value = new Date(`${date}T${time}`);
  value.setMinutes(value.getMinutes() + minutes);
  return value.toISOString();
}

function studentName(students: StudentLink[], studentId: string): string {
  return students.find((student) => student.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}

function durationMinutes(lesson: Lesson): string {
  const start = new Date(lesson.starts_at);
  const end = new Date(lesson.ends_at);
  if (isNaN(start.getTime()) || isNaN(end.getTime())) return "";
  return `${Math.max(0, Math.round((end.getTime() - start.getTime()) / 60000))} мин`;
}

function groupTitle(iso: string): string {
  const date = new Date(iso);
  if (isNaN(date.getTime())) return "Без даты";
  const today = new Date();
  const tomorrow = new Date();
  tomorrow.setDate(today.getDate() + 1);
  const key = date.toDateString();
  if (key === today.toDateString()) return "Сегодня";
  if (key === tomorrow.toDateString()) return "Завтра";
  return date.toLocaleDateString("ru-RU", { weekday: "long", day: "numeric", month: "long" });
}

export default function TeacherLessons() {
  const dashboard = useAsync<TeacherDashboard>(() => api.get("/dashboard/teacher"), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const [filter, setFilter] = useState<"all" | "scheduled" | "completed" | "cancelled">("all");
  const [query, setQuery] = useState("");
  const [createOpen, setCreateOpen] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);

  const list = lessons.data ?? [];
  const studentList = students.data ?? [];
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return list
      .filter((lesson) => filter === "all" || lesson.status === filter)
      .filter((lesson) => {
        if (!q) return true;
        return (
          studentName(studentList, lesson.student_id).toLowerCase().includes(q) ||
          (lesson.topic ?? "").toLowerCase().includes(q)
        );
      })
      .sort((a, b) => new Date(a.starts_at).getTime() - new Date(b.starts_at).getTime());
  }, [filter, list, query, studentList]);

  const groups = useMemo(() => {
    const map = new Map<string, Lesson[]>();
    filtered.forEach((lesson) => {
      const title = groupTitle(lesson.starts_at);
      map.set(title, [...(map.get(title) ?? []), lesson]);
    });
    return [...map.entries()];
  }, [filtered]);

  return (
    <AppShell
      title="Занятия"
      subtitle="Расписание и история"
      navSection="Работа"
      navItems={teacherNav("lessons", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={
        <button className="primary-action" type="button" onClick={() => setCreateOpen(true)}>
          <Icon name="add" />
          <span>Новое занятие</span>
        </button>
      }
    >
      <div className="container">
        <div className="teacher-toolbar">
          <div className="segmented">
            <button className={filter === "all" ? "active" : ""} onClick={() => setFilter("all")}>Все</button>
            <button className={filter === "scheduled" ? "active" : ""} onClick={() => setFilter("scheduled")}>Запланированные</button>
            <button className={filter === "completed" ? "active" : ""} onClick={() => setFilter("completed")}>Завершённые</button>
            <button className={filter === "cancelled" ? "active" : ""} onClick={() => setFilter("cancelled")}>Отменённые</button>
          </div>
          <div className="search-field">
            <Icon name="search" />
            <input placeholder="Поиск по ученику или теме…" value={query} onChange={(event) => setQuery(event.target.value)} />
          </div>
        </div>

        <Notice text={notice} />

        <Card title="Расписание" icon="calendar_month">
          {groups.map(([title, items]) => (
            <div className="lesson-group" key={title}>
              <div className="lesson-group-title">
                <span>{title}</span>
                <span className="hint">{items.length} занят.</span>
              </div>
              {items.map((lesson) => (
                <LessonRow
                  key={lesson.id}
                  lesson={lesson}
                  lessons={lessons}
                  students={studentList}
                  dashboard={dashboard}
                />
              ))}
            </div>
          ))}
          <ListState query={{ ...lessons, data: filtered }} empty="В этом разделе пока нет занятий." />
        </Card>
      </div>

      {createOpen && (
        <CreateLessonModal
          students={students}
          onClose={() => setCreateOpen(false)}
          onCreated={() => {
            setNotice("Занятие создано.");
            lessons.reload();
            dashboard.reload();
            setCreateOpen(false);
          }}
        />
      )}
    </AppShell>
  );
}

function LessonRow({
  lesson,
  lessons,
  students,
  dashboard,
}: {
  lesson: Lesson;
  lessons: Async<Lesson[]>;
  students: StudentLink[];
  dashboard: Async<TeacherDashboard>;
}) {
  const [rescheduleOpen, setRescheduleOpen] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const name = studentName(students, lesson.student_id);

  async function reload() {
    lessons.reload();
    dashboard.reload();
  }

  async function complete() {
    setBusy("complete");
    setError(null);
    setNotice(null);
    try {
      const result = await api.post<CompleteLessonResponse>(`/lessons/${lesson.id}/complete`);
      setNotice(result.charge_status === "pending" ? "Занятие завершено, начисление создаётся." : "Занятие завершено.");
      reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(null);
    }
  }

  async function cancel() {
    setBusy("cancel");
    setError(null);
    setNotice(null);
    try {
      await api.post(`/lessons/${lesson.id}/cancel`);
      setNotice("Занятие отменено.");
      reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(null);
    }
  }

  async function reactivate() {
    setBusy("reactivate");
    setError(null);
    setNotice(null);
    try {
      await api.post(`/lessons/${lesson.id}/reactivate`);
      setNotice("Занятие восстановлено.");
      reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(null);
    }
  }

  return (
    <div className="lesson-list-row">
      <div className="lesson-time">
        <strong>{timeOnly(lesson.starts_at)}</strong>
        <span>{durationMinutes(lesson)}</span>
      </div>
      <div className="avatar">{initials(name)}</div>
      <div className="lesson-info">
        <div className="lesson-title">{name}</div>
        <div className="muted">{lesson.topic || "Занятие"} · {fmtDate(lesson.starts_at)}</div>
        <ErrorMsg error={error} />
        <Notice text={notice} />
      </div>
      <StatusPill status={lesson.status} />
      <div className="btn-group">
        {lesson.status === "scheduled" && <button className="small primary" disabled={!!busy} onClick={complete}>{busy === "complete" ? "…" : "Завершить"}</button>}
        <div className="action-menu-wrap">
          <button className="icon-button compact" type="button" onClick={() => setMenuOpen((value) => !value)} title="Действия">
            <Icon name="more_vert" />
          </button>
          {menuOpen && (
            <div className="action-menu">
              {lesson.status === "scheduled" && (
                <>
                  <button type="button" onClick={() => { setMenuOpen(false); setRescheduleOpen(true); }}>
                    <Icon name="schedule" />Перенести
                  </button>
                  <button type="button" className="danger-menu-item" disabled={!!busy} onClick={() => { setMenuOpen(false); cancel(); }}>
                    <Icon name="event_busy" />Отменить
                  </button>
                </>
              )}
              {lesson.status === "completed" && (
                <button type="button" className="danger-menu-item" disabled={!!busy} onClick={() => { setMenuOpen(false); cancel(); }}>
                  <Icon name="event_busy" />Отменить
                </button>
              )}
              {lesson.status === "cancelled" && (
                <button type="button" disabled={!!busy} onClick={() => { setMenuOpen(false); reactivate(); }}>
                  <Icon name="restore" />Восстановить
                </button>
              )}
              {!["scheduled", "completed", "cancelled"].includes(lesson.status) && (
                <button type="button" disabled><Icon name="info" />Нет действий</button>
              )}
            </div>
          )}
        </div>
      </div>
      {rescheduleOpen && (
        <RescheduleLessonModal
          lesson={lesson}
          studentName={name}
          busy={busy === "reschedule"}
          onClose={() => setRescheduleOpen(false)}
          onSubmit={async (starts, ends) => {
            setBusy("reschedule");
            setError(null);
            setNotice(null);
            try {
              await api.post(`/lessons/${lesson.id}/reschedule`, {
                new_starts_at: toIso(starts),
                new_ends_at: toIso(ends),
              });
              setNotice("Занятие перенесено.");
              setRescheduleOpen(false);
              reload();
            } catch (err) {
              setError((err as Error).message);
            } finally {
              setBusy(null);
            }
          }}
        />
      )}
    </div>
  );
}

function RescheduleLessonModal({
  lesson,
  studentName,
  busy,
  onClose,
  onSubmit,
}: {
  lesson: Lesson;
  studentName: string;
  busy: boolean;
  onClose: () => void;
  onSubmit: (starts: string, ends: string) => Promise<void>;
}) {
  const [starts, setStarts] = useState(isoToLocalInput(lesson.starts_at));
  const [ends, setEnds] = useState(isoToLocalInput(lesson.ends_at));

  async function submit(event: FormEvent) {
    event.preventDefault();
    await onSubmit(starts, ends);
  }

  return (
    <div className="modal-overlay" onMouseDown={onClose}>
      <form className="modal-panel" onMouseDown={(event) => event.stopPropagation()} onSubmit={submit}>
        <div className="modal-heading">
          <div>
            <h2>Перенести занятие</h2>
            <p>{studentName} · {lesson.topic || "Занятие"}</p>
          </div>
          <button className="icon-button" type="button" onClick={onClose} title="Закрыть">
            <Icon name="close" />
          </button>
        </div>
        <div className="modal-fields">
          <div className="field">
            <label>Новое начало
              <input type="datetime-local" value={starts} onChange={(event) => setStarts(event.target.value)} required />
            </label>
          </div>
          <div className="field">
            <label>Новый конец
              <input type="datetime-local" value={ends} onChange={(event) => setEnds(event.target.value)} required />
            </label>
          </div>
        </div>
        <div className="modal-actions">
          <button type="button" onClick={onClose}>Отмена</button>
          <button className="primary" type="submit" disabled={busy}>
            <Icon name="schedule" />
            {busy ? "Перенос…" : "Перенести"}
          </button>
        </div>
      </form>
    </div>
  );
}

function CreateLessonModal({
  students,
  onClose,
  onCreated,
}: {
  students: Async<StudentLink[]>;
  onClose: () => void;
  onCreated: () => void;
}) {
  const [studentId, setStudentId] = useState("");
  const [date, setDate] = useState("");
  const [time, setTime] = useState("");
  const [duration, setDuration] = useState("60");
  const [topic, setTopic] = useState("");
  const [price, setPrice] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function create(event: FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await api.post("/lessons", {
        student_id: studentId,
        starts_at: toIsoFromParts(date, time),
        ends_at: addMinutesIso(date, time, Number(duration)),
        topic: topic || undefined,
        price: price ? Number(price) : undefined,
      });
      setStudentId("");
      setDate("");
      setTime("");
      setDuration("60");
      setTopic("");
      setPrice("");
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
            <h2>Новое занятие</h2>
            <p>Запланируйте занятие с учеником</p>
          </div>
          <button className="icon-button" type="button" onClick={onClose} title="Закрыть">
            <Icon name="close" />
          </button>
        </div>

        <ErrorMsg error={error} />

        <div className="modal-fields">
          <div className="field">
            <label>Ученик
              <span className="select-wrap">
                <select value={studentId} onChange={(event) => setStudentId(event.target.value)} required>
                  <option value="">— выбрать —</option>
                  {(students.data ?? []).map((student) => (
                    <option key={student.id} value={student.student_id}>{student.display_name}</option>
                  ))}
                </select>
                <Icon name="expand_more" />
              </span>
            </label>
          </div>

          <div className="field">
            <label>Тема занятия
              <input value={topic} onChange={(event) => setTopic(event.target.value)} placeholder="Например: Алгебра · производные" />
            </label>
          </div>

          <div className="field-row modal-field-row">
            <label>Дата
              <input type="date" value={date} onChange={(event) => setDate(event.target.value)} required />
            </label>
            <label className="time-field">Время
              <input type="time" value={time} onChange={(event) => setTime(event.target.value)} required />
            </label>
          </div>

          <div className="field-row modal-field-row">
            <label>Длительность
              <span className="select-wrap">
                <select value={duration} onChange={(event) => setDuration(event.target.value)} required>
                  <option value="45">45 минут</option>
                  <option value="60">60 минут</option>
                  <option value="90">90 минут</option>
                </select>
                <Icon name="expand_more" />
              </span>
            </label>
            <label>Стоимость
              <span className="amount-input">
                <input type="number" min="0" value={price} onChange={(event) => setPrice(event.target.value)} placeholder="из ставки" />
                <span>₽</span>
              </span>
            </label>
          </div>
        </div>

        <div className="modal-actions">
          <button type="button" onClick={onClose}>Отмена</button>
          <button className="primary" type="submit" disabled={busy || students.loading}>
            <Icon name="event_available" />
            {busy ? "Создание…" : "Создать занятие"}
          </button>
        </div>
      </form>
    </div>
  );
}
