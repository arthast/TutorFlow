import { useMemo, useState } from "react";
import { api, reports, type Lesson, type StudentDashboard } from "../api";
import { AppShell, Card, FileChips, Icon, ListState, StatusPill, useAsync } from "../ui";
import { lessonInterval, money, studentNav } from "./studentNav";

function teacherName(dashboard: StudentDashboard | null, teacherId: string): string {
  return dashboard?.summaries.find((summary) => summary.teacher_id === teacherId)?.teacher_name ?? teacherId.slice(0, 8);
}

export default function StudentLessons() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const [status, setStatus] = useState("scheduled");

  const activeAssignments = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.upcoming_lessons_count, 0) ?? 0;
  const filtered = useMemo(
    () => (lessons.data ?? []).filter((lesson) => status === "all" || lesson.status === status),
    [lessons.data, status],
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
          <div className="segmented">
            {[
              ["scheduled", "Ближайшие"],
              ["completed", "Проведённые"],
              ["cancelled", "Отменённые"],
              ["all", "Все"],
            ].map(([value, label]) => (
              <button className={status === value ? "active" : ""} key={value} onClick={() => setStatus(value)}>
                {label}
              </button>
            ))}
          </div>
        </div>

        <Card title="Занятия" icon="calendar_month">
          {filtered.map((lesson) => (
            <div className="resource-row" key={lesson.id}>
              <div className="resource-icon"><Icon name="school" /></div>
              <div className="resource-main">
                <div className="summary-title">{lesson.topic || "Занятие"}</div>
                <div className="summary-grid">
                  <span>{teacherName(dashboard.data, lesson.teacher_id)}</span>
                  <span>{lessonInterval(lesson.starts_at, lesson.ends_at)}</span>
                  {typeof lesson.price === "number" && <span>{money(lesson.price)}</span>}
                </div>
                <FileChips fileIds={lesson.file_ids} label="Материалы" />
              </div>
              <StatusPill status={lesson.status} />
            </div>
          ))}
          <ListState query={{ ...lessons, data: filtered }} empty="Занятия не найдены." />
        </Card>
      </div>
    </AppShell>
  );
}
