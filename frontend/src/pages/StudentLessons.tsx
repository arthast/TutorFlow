import { useMemo, useState } from "react";
import { api, reports, type Lesson, type StudentDashboard } from "../api";
import {
  AppShell,
  Avatar,
  EmptyState,
  ErrorState,
  FileChips,
  Segmented,
  SkeletonRows,
  StatusPill,
  useAsync,
  type TabItem,
} from "../ui";
import { money, studentNav } from "./studentNav";

type Segment = "scheduled" | "completed" | "cancelled" | "all";

function teacherName(dashboard: StudentDashboard | null, teacherId: string): string {
  return dashboard?.summaries.find((s) => s.teacher_id === teacherId)?.teacher_name ?? "Преподаватель";
}
function timeOnly(iso?: string): string {
  if (!iso) return "—";
  const d = new Date(iso);
  return isNaN(d.getTime()) ? "—" : d.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" });
}
function dateLabel(iso?: string): string {
  if (!iso) return "";
  const d = new Date(iso);
  return isNaN(d.getTime()) ? "" : d.toLocaleDateString("ru-RU", { day: "numeric", month: "long" });
}
function durationLabel(lesson: Lesson): string {
  const start = new Date(lesson.starts_at);
  const end = new Date(lesson.ends_at);
  if (isNaN(start.getTime()) || isNaN(end.getTime())) return "";
  return `${Math.max(0, Math.round((end.getTime() - start.getTime()) / 60000))} мин`;
}

export default function StudentLessons() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const [segment, setSegment] = useState<Segment>("scheduled");

  const list = lessons.data ?? [];
  const activeAssignments = dashboard.data?.summaries.reduce((s, i) => s + i.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((s, i) => s + i.activity.upcoming_lessons_count, 0) ?? 0;

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

  const filtered = useMemo(
    () =>
      list
        .filter((l) => segment === "all" || l.status === segment)
        .sort((a, b) => {
          const diff = new Date(a.starts_at).getTime() - new Date(b.starts_at).getTime();
          return segment === "scheduled" ? diff : -diff;
        }),
    [list, segment],
  );

  return (
    <AppShell
      title="Мои занятия"
      subtitle="Расписание и история"
      navSection="Учёба"
      accent="student"
      navItems={studentNav("lessons", {
        lessons: upcomingLessons,
        assignments: activeAssignments,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container">
        <div className="teacher-toolbar">
          <Segmented items={segments} active={segment} onChange={(k) => setSegment(k as Segment)} />
        </div>

        {lessons.loading && !lessons.data ? (
          <div className="card"><SkeletonRows count={4} /></div>
        ) : lessons.error ? (
          <ErrorState error={lessons.error} onRetry={lessons.reload} />
        ) : filtered.length === 0 ? (
          <EmptyState icon="event_busy" title="Здесь пусто" hint="В этом разделе пока нет занятий." />
        ) : (
          filtered.map((lesson) => (
            <div className={"lesson-card status-" + lesson.status} key={lesson.id}>
              <div className="lesson-time">
                <strong>{timeOnly(lesson.starts_at)}</strong>
                <span>{durationLabel(lesson)}</span>
              </div>
              <Avatar name={teacherName(dashboard.data, lesson.teacher_id)} tone="teacher" />
              <div className="lesson-info">
                <div className="lesson-title">{lesson.topic || "Занятие"}</div>
                <div className="muted">
                  {teacherName(dashboard.data, lesson.teacher_id)} · {dateLabel(lesson.starts_at)}
                </div>
                <FileChips fileIds={lesson.file_ids} label="Материалы" />
              </div>
              {typeof lesson.price === "number" && <span className="lesson-price">{money(lesson.price)}</span>}
              <StatusPill status={lesson.status} />
            </div>
          ))
        )}
      </div>
    </AppShell>
  );
}
