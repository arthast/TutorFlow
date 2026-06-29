import { useEffect, useMemo, useRef, useState } from "react";
import {
  ApiError,
  api,
  type FileMeta,
  type CompleteLessonResponse,
  type Lesson,
  type StudentLink,
  type TeacherDashboard,
} from "../api";
import {
  AppShell,
  Avatar,
  Button,
  EmptyState,
  ErrorMsg,
  Field,
  FileChips,
  Icon,
  Modal,
  Segmented,
  SkeletonRows,
  StatusPill,
  Select,
  useAsync,
  useToast,
  type TabItem,
} from "../ui";
import { useOnlineStatus, useRealtimeEvent } from "../realtime";
import { teacherNav, money } from "./teacherNav";

type Segment = "scheduled" | "completed" | "cancelled" | "all";

const TONES = ["teacher", "student", "amber", "muted"] as const;
type Tone = (typeof TONES)[number];

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

function toIsoFromParts(date: string, time: string): string {
  return new Date(`${date}T${time}`).toISOString();
}
function addMinutesIso(date: string, time: string, minutes: number): string {
  const value = new Date(`${date}T${time}`);
  value.setMinutes(value.getMinutes() + minutes);
  return value.toISOString();
}
function isoToDateInput(iso: string): string {
  const d = new Date(iso);
  return isNaN(d.getTime()) ? "" : new Date(d.getTime() - d.getTimezoneOffset() * 60000).toISOString().slice(0, 10);
}
function isoToTimeInput(iso: string): string {
  const d = new Date(iso);
  return isNaN(d.getTime()) ? "" : new Date(d.getTime() - d.getTimezoneOffset() * 60000).toISOString().slice(11, 16);
}
function timeOnly(iso?: string): string {
  if (!iso) return "—";
  const d = new Date(iso);
  return isNaN(d.getTime()) ? "—" : d.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" });
}
function durationLabel(lesson: Lesson): string {
  const start = new Date(lesson.starts_at);
  const end = new Date(lesson.ends_at);
  if (isNaN(start.getTime()) || isNaN(end.getTime())) return "";
  return `${Math.max(0, Math.round((end.getTime() - start.getTime()) / 60000))} мин`;
}
function studentName(students: StudentLink[], studentId: string): string {
  return students.find((s) => s.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}
function groupTitle(iso: string): string {
  const date = new Date(iso);
  if (isNaN(date.getTime())) return "Без даты";
  const today = new Date();
  const tomorrow = new Date();
  tomorrow.setDate(today.getDate() + 1);
  if (date.toDateString() === today.toDateString()) return "Сегодня";
  if (date.toDateString() === tomorrow.toDateString()) return "Завтра";
  return date.toLocaleDateString("ru-RU", { weekday: "long", day: "numeric", month: "long" });
}

export default function TeacherLessons() {
  const dashboard = useAsync<TeacherDashboard>(() => api.get("/dashboard/teacher"), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const [segment, setSegment] = useState<Segment>("scheduled");
  const [query, setQuery] = useState("");
  const [createOpen, setCreateOpen] = useState(false);

  const list = lessons.data ?? [];
  const studentList = students.data ?? [];

  function reloadAll() {
    lessons.reload();
    dashboard.reload();
  }

  useRealtimeEvent((event) => {
    if (event.type.startsWith("lesson")) reloadAll();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const counts = useMemo(
    () => ({
      scheduled: list.filter((l) => l.status === "scheduled").length,
      completed: list.filter((l) => l.status === "completed").length,
      cancelled: list.filter((l) => l.status === "cancelled").length,
      all: list.length,
    }),
    [list],
  );

  const segments: TabItem[] = [
    { key: "scheduled", label: "Ближайшие", count: counts.scheduled },
    { key: "completed", label: "Проведённые", count: counts.completed },
    { key: "cancelled", label: "Отменённые", count: counts.cancelled },
    { key: "all", label: "Все", count: counts.all },
  ];

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return list
      .filter((l) => segment === "all" || l.status === segment)
      .filter((l) => {
        if (!q) return true;
        return studentName(studentList, l.student_id).toLowerCase().includes(q) || (l.topic ?? "").toLowerCase().includes(q);
      })
      .sort((a, b) => {
        const diff = new Date(a.starts_at).getTime() - new Date(b.starts_at).getTime();
        return segment === "scheduled" ? diff : -diff;
      });
  }, [list, studentList, segment, query]);

  const groups = useMemo(() => {
    const map = new Map<string, Lesson[]>();
    filtered.forEach((l) => map.set(groupTitle(l.starts_at), [...(map.get(groupTitle(l.starts_at)) ?? []), l]));
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
        <Button variant="primary" icon="add" onClick={() => setCreateOpen(true)}>Новое занятие</Button>
      }
    >
      <div className="container">
        <div className="teacher-toolbar">
          <Segmented items={segments} active={segment} onChange={(k) => setSegment(k as Segment)} />
          <div className="search-field">
            <Icon name="search" />
            <input placeholder="Поиск по ученику или теме…" value={query} onChange={(e) => setQuery(e.target.value)} />
          </div>
        </div>

        {lessons.loading && !lessons.data ? (
          <div className="card"><SkeletonRows count={4} /></div>
        ) : lessons.error ? (
          <ErrorMsg error={lessons.error} />
        ) : groups.length === 0 ? (
          <EmptyState icon="event_busy" title="Здесь пусто" hint="В этом разделе пока нет занятий." />
        ) : (
          groups.map(([title, items]) => (
            <div className="lesson-group" key={title}>
              <div className="lesson-group-title">
                <span>{title}</span>
                <span className="hint">{items.length} занят.</span>
              </div>
              {items.map((lesson, i) => (
                <LessonCard
                  key={lesson.id}
                  lesson={lesson}
                  tone={TONES[i % TONES.length]}
                  name={studentName(studentList, lesson.student_id)}
                  onChanged={reloadAll}
                />
              ))}
            </div>
          ))
        )}
      </div>

      {createOpen && (
        <CreateLessonModal
          students={studentList}
          onClose={() => setCreateOpen(false)}
          onCreated={() => {
            setCreateOpen(false);
            reloadAll();
          }}
        />
      )}
    </AppShell>
  );
}

function LessonCard({
  lesson,
  tone,
  name,
  onChanged,
}: {
  lesson: Lesson;
  tone: Tone;
  name: string;
  onChanged: () => void;
}) {
  const toast = useToast();
  const online = useOnlineStatus(lesson.student_id);
  const [busy, setBusy] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const [rescheduleOpen, setRescheduleOpen] = useState(false);
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!menuOpen) return;
    function onDocClick(e: MouseEvent) {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) setMenuOpen(false);
    }
    document.addEventListener("mousedown", onDocClick);
    return () => document.removeEventListener("mousedown", onDocClick);
  }, [menuOpen]);

  function fail(err: unknown) {
    const isConflict = err instanceof ApiError && err.status === 409;
    toast({
      tone: "danger",
      title: isConflict ? "Время занято" : "Не удалось",
      body: (err as Error).message,
    });
  }

  async function run(action: () => Promise<void>) {
    setBusy(true);
    setMenuOpen(false);
    try {
      await action();
      onChanged();
    } catch (err) {
      fail(err);
    } finally {
      setBusy(false);
    }
  }

  function complete() {
    run(async () => {
      const result = await api.post<CompleteLessonResponse>(`/lessons/${lesson.id}/complete`);
      toast({
        tone: "success",
        title: "Занятие завершено",
        body: result.charge_status === "pending" ? `${name} · начисление создаётся` : name,
      });
    });
  }
  function cancel() {
    run(async () => {
      await api.post(`/lessons/${lesson.id}/cancel`);
      toast({ tone: "warning", title: "Занятие отменено", body: name });
    });
  }
  function reactivate() {
    run(async () => {
      await api.post(`/lessons/${lesson.id}/reactivate`);
      toast({ tone: "info", title: "Занятие восстановлено", body: `${name} · снова запланировано` });
    });
  }

  const actions: { icon: string; label: string; danger?: boolean; run: () => void }[] = [];
  if (lesson.status === "scheduled") {
    actions.push({ icon: "schedule", label: "Перенести время", run: () => { setMenuOpen(false); setRescheduleOpen(true); } });
    actions.push({ icon: "cancel", label: "Отменить занятие", danger: true, run: cancel });
  } else if (lesson.status === "completed") {
    actions.push({ icon: "cancel", label: "Отменить занятие", danger: true, run: cancel });
  } else if (lesson.status === "cancelled") {
    actions.push({ icon: "restart_alt", label: "Восстановить", run: reactivate });
  }

  return (
    <div className={"lesson-card status-" + lesson.status}>
      <div className="lesson-time">
        <strong>{timeOnly(lesson.starts_at)}</strong>
        <span>{durationLabel(lesson)}</span>
      </div>
      <Avatar name={name} tone={tone} presence={online && lesson.status === "scheduled" ? "online" : undefined} />
      <div className="lesson-info">
        <div className="lesson-title">{name}</div>
        <div className="muted">{lesson.topic || "Занятие"}</div>
        <FileChips fileIds={lesson.file_ids} label="Материалы" />
      </div>
      {typeof lesson.price === "number" && <span className="lesson-price">{money(lesson.price)}</span>}
      <StatusPill status={lesson.status} />
      {lesson.status === "scheduled" && (
        <Button variant="primary" size="sm" loading={busy} onClick={complete}>Завершить</Button>
      )}
      {lesson.status === "cancelled" && (
        <Button variant="secondary" size="sm" loading={busy} onClick={reactivate}>Восстановить</Button>
      )}
      <div className="action-menu-wrap" ref={menuRef}>
        <button className="icon-button compact" type="button" title="Действия" onClick={() => setMenuOpen((v) => !v)}>
          <Icon name="more_horiz" />
        </button>
        {menuOpen && (
          <div className="action-menu">
            {actions.map((a) => (
              <button key={a.label} type="button" className={a.danger ? "danger-menu-item" : ""} disabled={busy} onClick={a.run}>
                <Icon name={a.icon} />{a.label}
              </button>
            ))}
          </div>
        )}
      </div>

      {rescheduleOpen && (
        <RescheduleModal
          lesson={lesson}
          name={name}
          onClose={() => setRescheduleOpen(false)}
          onDone={() => {
            setRescheduleOpen(false);
            onChanged();
          }}
        />
      )}
    </div>
  );
}

const DURATIONS = [
  { key: "45", label: "45 минут" },
  { key: "60", label: "60 минут" },
  { key: "90", label: "90 минут" },
];

function CreateLessonModal({
  students,
  onClose,
  onCreated,
}: {
  students: StudentLink[];
  onClose: () => void;
  onCreated: () => void;
}) {
  const toast = useToast();
  const [studentId, setStudentId] = useState("");
  const [topic, setTopic] = useState("");
  const [date, setDate] = useState("");
  const [time, setTime] = useState("");
  const [duration, setDuration] = useState("60");
  const [price, setPrice] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const selected = students.find((s) => s.student_id === studentId);
  const ratePlaceholder = typeof selected?.hourly_rate === "number" ? `из ставки · ${Math.round(selected.hourly_rate)} ₽` : "из ставки";

  async function submit() {
    setBusy(true);
    setError(null);
    try {
      const fileIds = await uploadAll(files, "lesson_material");
      await api.post("/lessons", {
        student_id: studentId,
        starts_at: toIsoFromParts(date, time),
        ends_at: addMinutesIso(date, time, Number(duration)),
        topic: topic || undefined,
        price: price ? Number(price) : undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      toast({ tone: "success", title: "Занятие создано", body: "Добавлено в расписание" });
      onCreated();
    } catch (err) {
      const conflict = err instanceof ApiError && err.status === 409;
      setError((err as Error).message);
      if (conflict) toast({ tone: "danger", title: "Время занято", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      title="Новое занятие"
      subtitle="Запланируйте занятие с учеником"
      onClose={onClose}
      onSubmit={submit}
      footer={
        <>
          <Button type="button" onClick={onClose}>Отмена</Button>
          <Button variant="primary" type="submit" icon="event_available" loading={busy} disabled={!studentId || !date || !time}>
            Создать занятие
          </Button>
        </>
      }
    >
      <ErrorMsg error={error} />
      <div className="modal-fields">
        <Field label="Ученик">
          <Select value={studentId} onChange={(e) => setStudentId(e.target.value)} required>
            <option value="">— выбрать —</option>
            {students.map((s) => <option key={s.id} value={s.student_id}>{s.display_name}</option>)}
          </Select>
        </Field>
        <Field label="Тема занятия">
          <input value={topic} onChange={(e) => setTopic(e.target.value)} placeholder="Например: Алгебра · производные" />
        </Field>
        <div className="field-row modal-field-row">
          <Field label="Дата">
            <input type="date" value={date} onChange={(e) => setDate(e.target.value)} required />
          </Field>
          <Field label="Время" className="time-field">
            <input type="time" value={time} onChange={(e) => setTime(e.target.value)} required />
          </Field>
        </div>
        <div className="field-row modal-field-row">
          <Field label="Длительность">
            <Select value={duration} onChange={(e) => setDuration(e.target.value)}>
              {DURATIONS.map((d) => <option key={d.key} value={d.key}>{d.label}</option>)}
            </Select>
          </Field>
          <Field label="Стоимость">
            <span className="amount-input">
              <input type="number" min="0" value={price} onChange={(e) => setPrice(e.target.value)} placeholder={ratePlaceholder} />
              <span>₽</span>
            </span>
          </Field>
        </div>
        <Field label="Материалы (необязательно)" hint="PDF, изображения и др. — увидит ученик">
          <input type="file" multiple onChange={(e) => setFiles(e.target.files ? Array.from(e.target.files) : [])} />
        </Field>
      </div>
    </Modal>
  );
}

function RescheduleModal({
  lesson,
  name,
  onClose,
  onDone,
}: {
  lesson: Lesson;
  name: string;
  onClose: () => void;
  onDone: () => void;
}) {
  const toast = useToast();
  const [date, setDate] = useState(isoToDateInput(lesson.starts_at));
  const [time, setTime] = useState(isoToTimeInput(lesson.starts_at));
  const initialDuration = Math.max(0, Math.round((new Date(lesson.ends_at).getTime() - new Date(lesson.starts_at).getTime()) / 60000));
  const [duration, setDuration] = useState(String(initialDuration || 60));
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit() {
    setBusy(true);
    setError(null);
    try {
      await api.post(`/lessons/${lesson.id}/reschedule`, {
        new_starts_at: toIsoFromParts(date, time),
        new_ends_at: addMinutesIso(date, time, Number(duration)),
      });
      toast({ tone: "info", title: "Время перенесено", body: `${name} · ученик уведомлён` });
      onDone();
    } catch (err) {
      const conflict = err instanceof ApiError && err.status === 409;
      setError((err as Error).message);
      if (conflict) toast({ tone: "danger", title: "Время занято", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      title="Перенести занятие"
      subtitle={`${name} · ${lesson.topic || "Занятие"}`}
      onClose={onClose}
      onSubmit={submit}
      footer={
        <>
          <Button type="button" onClick={onClose}>Отмена</Button>
          <Button variant="primary" type="submit" icon="schedule" loading={busy} disabled={!date || !time}>Перенести</Button>
        </>
      }
    >
      <ErrorMsg error={error} />
      <div className="modal-fields">
        <div className="field-row modal-field-row">
          <Field label="Дата">
            <input type="date" value={date} onChange={(e) => setDate(e.target.value)} required />
          </Field>
          <Field label="Время" className="time-field">
            <input type="time" value={time} onChange={(e) => setTime(e.target.value)} required />
          </Field>
        </div>
        <Field label="Длительность">
          <Select value={duration} onChange={(e) => setDuration(e.target.value)}>
            {DURATIONS.map((d) => <option key={d.key} value={d.key}>{d.label}</option>)}
          </Select>
        </Field>
      </div>
    </Modal>
  );
}
