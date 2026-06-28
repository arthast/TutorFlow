import { useMemo, useState, type FormEvent } from "react";
import {
  api,
  reports,
  type Assignment,
  type AssignmentDetail,
  type FileMeta,
  type StudentDashboard,
} from "../api";
import { AppShell, Card, ErrorMsg, FileChips, Icon, ListState, Notice, StatusPill, useAsync } from "../ui";
import { studentNav } from "./studentNav";

export default function StudentAssignments() {
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const [status, setStatus] = useState("active");
  const [openId, setOpenId] = useState<string | null>(null);

  const activeAssignments = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((sum, item) => sum + item.activity.upcoming_lessons_count, 0) ?? 0;
  const filtered = useMemo(
    () => (assignments.data ?? []).filter((assignment) => {
      if (status === "active") return assignment.status === "assigned" || assignment.status === "needs_fix";
      if (status === "all") return true;
      return assignment.status === status;
    }),
    [assignments.data, status],
  );

  return (
    <AppShell
      title="Домашние задания"
      subtitle="Сдача решений и комментарии"
      navSection="Учёба"
      accent="student"
      navItems={studentNav("assignments", {
        lessons: upcomingLessons,
        assignments: activeAssignments,
        receipts: dashboard.data?.pending_receipts_count,
      })}
    >
      <div className="container">
        <div className="teacher-toolbar">
          <div className="segmented">
            {[
              ["active", "К сдаче"],
              ["submitted", "Сдано"],
              ["needs_fix", "На правках"],
              ["reviewed", "Проверено"],
              ["all", "Все"],
            ].map(([value, label]) => (
              <button className={status === value ? "active" : ""} key={value} onClick={() => setStatus(value)}>
                {label}
              </button>
            ))}
          </div>
        </div>

        <Card title="Задания" icon="assignment">
          {filtered.map((assignment) => (
            <div key={assignment.id}>
              <div className="resource-row">
                <div className="resource-icon"><Icon name="assignment" /></div>
                <button className="resource-main resource-plain-button" type="button" onClick={() => setOpenId(openId === assignment.id ? null : assignment.id)}>
                  <span className="summary-title">{assignment.title}</span>
                  <span className="summary-grid">
                    <span>{assignment.description || "Описание не указано"}</span>
                    <span>{assignment.created_at ? new Date(assignment.created_at).toLocaleDateString("ru-RU") : "-"}</span>
                  </span>
                </button>
                <StatusPill status={assignment.status} />
              </div>
              {openId === assignment.id && (
                <SubmitPanel
                  assignmentId={assignment.id}
                  onChanged={() => {
                    assignments.reload();
                    dashboard.reload();
                  }}
                />
              )}
            </div>
          ))}
          <ListState query={{ ...assignments, data: filtered }} empty="Задания не найдены." />
        </Card>
      </div>
    </AppShell>
  );
}

function SubmitPanel({ assignmentId, onChanged }: { assignmentId: string; onChanged: () => void }) {
  const detail = useAsync<AssignmentDetail>(() => api.get(`/assignments/${assignmentId}`), [assignmentId]);
  const [text, setText] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError(null);
    setNotice(null);
    setBusy(true);
    try {
      const fileIds: string[] = [];
      for (const file of files) {
        const form = new FormData();
        form.append("file", file);
        form.append("purpose", "submission_file");
        const meta = await api.upload<FileMeta>("/files", form);
        fileIds.push(meta.id);
      }
      await api.post(`/assignments/${assignmentId}/submit`, {
        text_answer: text || undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      setNotice("Решение отправлено.");
      setText("");
      setFiles([]);
      detail.reload();
      onChanged();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  const assignment = detail.data;

  return (
    <div className="inline-panel">
      <ErrorMsg error={error || detail.error} />
      <Notice text={notice} />
      {detail.loading && !assignment && <p className="hint">Загрузка...</p>}
      {assignment?.description && <p className="review-text">{assignment.description}</p>}
      <FileChips fileIds={assignment?.file_ids} label="Материалы ДЗ" />
      {(assignment?.submissions ?? []).map((submission) => (
        <div className="summary-row" key={submission.id}>
          <div>
            <div className="summary-title">Моё решение</div>
            <div className="muted">{submission.text_answer || "Без текста"}</div>
            <FileChips fileIds={submission.file_ids} />
          </div>
          <StatusPill status={submission.status} />
        </div>
      ))}
      {(assignment?.comments ?? []).map((comment) => (
        <div className="row" key={comment.id}>
          <span>{comment.text}</span>
          <span className="hint">{comment.created_at ? new Date(comment.created_at).toLocaleDateString("ru-RU") : ""}</span>
        </div>
      ))}
      <form onSubmit={submit}>
        <div className="field"><label>Текст решения<textarea value={text} onChange={(event) => setText(event.target.value)} /></label></div>
        <div className="field"><label>Файлы<input type="file" multiple onChange={(event) => setFiles(event.target.files ? Array.from(event.target.files) : [])} /></label></div>
        <button className="primary" type="submit" disabled={busy}>{busy ? "Отправка..." : "Отправить решение"}</button>
      </form>
    </div>
  );
}
