import { useMemo, useState } from "react";
import {
  api,
  reports,
  type Assignment,
  type AssignmentDetail,
  type FileMeta,
  type StudentDashboard,
} from "../api";
import {
  AppShell,
  Button,
  CommentThread,
  EmptyState,
  ErrorState,
  FileChips,
  Icon,
  Segmented,
  SkeletonRows,
  StatusPill,
  fmtDate,
  useAsync,
  useToast,
  type TabItem,
} from "../ui";
import { useAuth } from "../auth";
import { useRealtimeEvent } from "../realtime";
import { studentNav } from "./studentNav";

type Filter = "todo" | "submitted" | "reviewed" | "all";

const STATUS_ICON: Record<string, { icon: string; tone: string }> = {
  assigned: { icon: "assignment", tone: "assignment-icon-info" },
  needs_fix: { icon: "edit_note", tone: "assignment-icon-warning" },
  submitted: { icon: "assignment_turned_in", tone: "assignment-icon-warning" },
  reviewed: { icon: "task_alt", tone: "assignment-icon-success" },
  accepted: { icon: "task_alt", tone: "assignment-icon-success" },
  done: { icon: "task_alt", tone: "assignment-icon-success" },
  expired: { icon: "event_busy", tone: "assignment-icon-danger" },
};
const TODO = new Set(["assigned", "needs_fix"]);
const REVIEWED = new Set(["reviewed", "accepted", "done"]);

function iconMeta(status: string) {
  return STATUS_ICON[status] ?? STATUS_ICON.assigned;
}
function deadlineLabel(a: Assignment): string {
  if (a.due_at) {
    const d = new Date(a.due_at);
    if (!isNaN(d.getTime())) return `дедлайн ${d.toLocaleString("ru-RU", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" })}`;
  }
  return "без дедлайна";
}
function isPastDeadline(a: Assignment): boolean {
  if (!a.due_at) return false;
  const d = new Date(a.due_at);
  return !isNaN(d.getTime()) && d.getTime() < Date.now();
}

export default function StudentAssignments() {
  const { user } = useAuth();
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const [filter, setFilter] = useState<Filter>("todo");
  const [openId, setOpenId] = useState<string | null>(null);

  const list = assignments.data ?? [];
  const activeAssignments = dashboard.data?.summaries.reduce((s, i) => s + i.activity.active_assignments_count, 0) ?? 0;
  const upcomingLessons = dashboard.data?.summaries.reduce((s, i) => s + i.activity.upcoming_lessons_count, 0) ?? 0;

  useRealtimeEvent((event) => {
    if (["assignment", "submission", "review"].some((t) => event.type.startsWith(t))) {
      assignments.reload();
      dashboard.reload();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const counts = useMemo(
    () => ({
      todo: list.filter((a) => TODO.has(a.status)).length,
      submitted: list.filter((a) => a.status === "submitted").length,
      reviewed: list.filter((a) => REVIEWED.has(a.status)).length,
      all: list.length,
    }),
    [list],
  );

  const filtered = useMemo(
    () =>
      list.filter((a) => {
        if (filter === "todo") return TODO.has(a.status);
        if (filter === "submitted") return a.status === "submitted";
        if (filter === "reviewed") return REVIEWED.has(a.status);
        return true;
      }),
    [list, filter],
  );

  const tabs: TabItem[] = [
    { key: "todo", label: "К сдаче", count: counts.todo },
    { key: "submitted", label: "Сдано", count: counts.submitted },
    { key: "reviewed", label: "Проверено", count: counts.reviewed },
    { key: "all", label: "Все", count: counts.all },
  ];

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
          <Segmented items={tabs} active={filter} onChange={(k) => setFilter(k as Filter)} />
        </div>

        {assignments.loading && !assignments.data ? (
          <div className="card"><SkeletonRows count={4} /></div>
        ) : assignments.error ? (
          <ErrorState error={assignments.error} onRetry={assignments.reload} />
        ) : filtered.length === 0 ? (
          <EmptyState icon="assignment" title="Заданий нет" hint="В этой вкладке пока пусто." />
        ) : (
          <div className="assignment-list">
            {filtered.map((a) => (
              <AssignmentItem
                key={a.id}
                assignment={a}
                open={openId === a.id}
                selfId={user?.user_id}
                onToggle={() => setOpenId(openId === a.id ? null : a.id)}
                onChanged={() => {
                  assignments.reload();
                  dashboard.reload();
                }}
              />
            ))}
          </div>
        )}
      </div>
    </AppShell>
  );
}

function AssignmentItem({
  assignment,
  open,
  selfId,
  onToggle,
  onChanged,
}: {
  assignment: Assignment;
  open: boolean;
  selfId?: string;
  onToggle: () => void;
  onChanged: () => void;
}) {
  const meta = iconMeta(assignment.status);
  const expired = assignment.status === "expired" || (TODO.has(assignment.status) && isPastDeadline(assignment));
  const accent = assignment.status === "needs_fix" ? "#d99413" : expired ? "#e0584f" : REVIEWED.has(assignment.status) ? "#18a866" : assignment.status === "submitted" ? "#d99413" : "#0f8a8a";

  return (
    <div>
      <div className="assignment-list-row" style={{ borderLeftColor: accent }}>
        <div className={"assignment-row-icon " + meta.tone}><Icon name={meta.icon} /></div>
        <button className="assignment-row-main" type="button" onClick={onToggle}>
          <div className="assignment-row-title">
            <span className="assignment-title-text">{assignment.title}</span>
            {!!assignment.file_ids?.length && (
              <span className="file-count"><Icon name="attach_file" />{assignment.file_ids.length}</span>
            )}
          </div>
          <div className="assignment-row-meta">
            <span className={expired ? "deadline danger" : ""}><Icon name="schedule" />{deadlineLabel(assignment)}</span>
          </div>
        </button>
        <StatusPill status={assignment.status} />
        <Button variant={open ? "secondary" : "ghost"} size="sm" icon={open ? "expand_less" : "expand_more"} onClick={onToggle}>
          {open ? "Скрыть" : "Открыть"}
        </Button>
      </div>
      {open && <AssignmentDetailPanel assignmentId={assignment.id} status={assignment.status} expired={expired} selfId={selfId} onChanged={onChanged} />}
    </div>
  );
}

function AssignmentDetailPanel({
  assignmentId,
  status,
  expired,
  selfId,
  onChanged,
}: {
  assignmentId: string;
  status: string;
  expired: boolean;
  selfId?: string;
  onChanged: () => void;
}) {
  const toast = useToast();
  const detail = useAsync<AssignmentDetail>(() => api.get(`/assignments/${assignmentId}`), [assignmentId]);
  const [text, setText] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [busy, setBusy] = useState(false);

  const a = detail.data;
  const canSubmit = TODO.has(status) && !expired;
  const resubmit = status === "needs_fix";

  async function submit() {
    if (!text.trim() && files.length === 0) {
      toast({ tone: "danger", title: "Пусто", body: "Добавьте текст или файлы решения" });
      return;
    }
    setBusy(true);
    try {
      const fileIds: string[] = [];
      for (const file of files) {
        const form = new FormData();
        form.append("file", file);
        form.append("purpose", "submission_file");
        const fileMeta = await api.upload<FileMeta>("/files", form);
        fileIds.push(fileMeta.id);
      }
      await api.post(`/assignments/${assignmentId}/submit`, {
        text_answer: text.trim() || undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      toast({ tone: "success", title: "Решение отправлено", body: "Преподаватель получит уведомление" });
      setText("");
      setFiles([]);
      detail.reload();
      onChanged();
    } catch (err) {
      toast({ tone: "danger", title: "Не удалось отправить", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="inline-panel">
      {detail.loading && !a ? (
        <SkeletonRows count={2} />
      ) : detail.error ? (
        <ErrorState error={detail.error} onRetry={detail.reload} />
      ) : (
        <>
          {a?.description && <p className="review-text">{a.description}</p>}
          <FileChips fileIds={a?.file_ids} label="Материалы задания" />

          {(a?.submissions ?? []).length > 0 && (
            <>
              <p className="section-title">Моё решение</p>
              {(a?.submissions ?? []).map((s) => (
                <div className="submission-summary" key={s.id}>
                  <div className="avatar tone-student"><Icon name="description" /></div>
                  <div>
                    <strong>{s.text_answer || "Решение"}</strong>
                    <span>Статус: <StatusPill status={s.status} /> · {fmtDate(s.submitted_at)}</span>
                    <FileChips fileIds={s.file_ids} />
                  </div>
                </div>
              ))}
            </>
          )}

          <p className="section-title">Комментарии</p>
          <CommentThread
            comments={a?.comments ?? []}
            teacherId={a?.teacher_id}
            selfId={selfId}
            onSubmit={async (value) => {
              await api.post(`/assignments/${assignmentId}/comments`, { text: value });
              detail.reload();
            }}
          />

          {canSubmit ? (
            <div className="submit-box">
              <p className="section-title">{resubmit ? "Сдать заново" : "Отправить решение"}</p>
              <div className="field">
                <label>Текст решения (необязательно)
                  <textarea value={text} onChange={(e) => setText(e.target.value)} placeholder="Комментарий к решению…" />
                </label>
              </div>
              <div className="field">
                <label>Файлы решения (можно несколько)
                  <input type="file" multiple onChange={(e) => setFiles(e.target.files ? Array.from(e.target.files) : [])} />
                </label>
              </div>
              {files.length > 0 && <p className="hint">Выбрано файлов: {files.length}</p>}
              <Button variant="primary" icon="send" loading={busy} onClick={submit}>
                {resubmit ? "Сдать заново" : "Отправить решение"}
              </Button>
            </div>
          ) : expired ? (
            <div className="notice-row danger"><Icon name="event_busy" />Дедлайн прошёл — сдача недоступна.</div>
          ) : status === "submitted" ? (
            <div className="notice-row"><Icon name="hourglass_top" />Решение отправлено, ждёт проверки.</div>
          ) : REVIEWED.has(status) ? (
            <div className="notice-row success"><Icon name="task_alt" />Работа проверена преподавателем.</div>
          ) : null}
        </>
      )}
    </div>
  );
}
