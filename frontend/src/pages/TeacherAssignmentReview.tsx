import { useMemo, useState } from "react";
import { Link, Navigate, useParams } from "react-router-dom";
import {
  api,
  type AssignmentDetail,
  type StudentLink,
  type Submission,
  type TeacherDashboard,
} from "../api";
import {
  AppShell,
  Button,
  CommentThread,
  ErrorMsg,
  FileChips,
  Icon,
  SkeletonRows,
  StatusPill,
  fmtDate,
  useAsync,
  useToast,
} from "../ui";
import { useAuth } from "../auth";
import { useRealtimeEvent } from "../realtime";
import { initials, teacherNav } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;
type Verdict = "reviewed" | "needs_fix";

function latestSubmission(submissions?: Submission[]): Submission | null {
  if (!submissions || submissions.length === 0) return null;
  return [...submissions].sort((a, b) => new Date(b.submitted_at ?? "").getTime() - new Date(a.submitted_at ?? "").getTime())[0];
}
function studentName(students: StudentLink[], studentId?: string): string {
  if (!studentId) return "Ученик";
  return students.find((s) => s.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}

export default function TeacherAssignmentReview() {
  const { assignmentId } = useParams();
  const { user } = useAuth();
  const dashboard = useAsync<TeacherDashboard>(() => api.get("/dashboard/teacher"), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const detail = useAsync<AssignmentDetail>(() => api.get(`/assignments/${assignmentId}`), [assignmentId]);

  useRealtimeEvent((event) => {
    if (["assignment", "submission", "review"].some((t) => event.type.startsWith(t))) detail.reload();
  }, [detail.reload]);

  if (!assignmentId) return <Navigate to="/teacher/assignments" replace />;

  const assignment = detail.data;
  const submission = latestSubmission(assignment?.submissions);
  const name = studentName(students.data ?? [], assignment?.student_id);

  return (
    <AppShell
      title="Проверка работы"
      subtitle={assignment?.title ?? "Домашнее задание"}
      navSection="Работа"
      navItems={teacherNav("assignments", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={<Link className="button-like" to="/teacher/assignments"><Icon name="arrow_back" />Домашние задания</Link>}
    >
      <div className="review-layout">
        <div className="dashboard-column">
          <section className="review-card review-assignment-card">
            <div className="review-card-main">
              <div className="review-card-icon"><Icon name="assignment" /></div>
              <div>
                <div className="review-title-row">
                  <h2>{assignment?.title ?? "Домашнее задание"}</h2>
                  {assignment?.status && <StatusPill status={assignment.status} />}
                </div>
                <div className="review-meta-row">
                  <span><Icon name="person" />{name}</span>
                  <span><Icon name="event" />{assignment?.due_at ? `дедлайн ${fmtDate(assignment.due_at)}` : "без дедлайна"}</span>
                </div>
                {assignment?.description && <p className="review-text">{assignment.description}</p>}
                <FileChips fileIds={assignment?.file_ids} label="Материалы задания" />
              </div>
            </div>
            <ErrorMsg error={detail.error} />
            {detail.loading && !assignment && <SkeletonRows count={2} />}
          </section>

          <section className="review-card">
            <div className="review-section-heading">
              <Icon name="upload_file" />
              <h3>Решение ученика</h3>
              {submission?.submitted_at && <span>{fmtDate(submission.submitted_at)}</span>}
            </div>
            {submission ? (
              <>
                <div className="submission-summary">
                  <div className="avatar tone-student">{initials(name)}</div>
                  <div>
                    <strong>{name}</strong>
                    <span>Статус решения: <StatusPill status={submission.status} /></span>
                  </div>
                </div>
                {submission.text_answer && (
                  <div className="student-comment">
                    <Icon name="chat" />
                    <span>{submission.text_answer}</span>
                  </div>
                )}
                <FileChips fileIds={submission.file_ids} label="Файлы решения" />
              </>
            ) : (
              <p className="hint">Ученик ещё не отправил решение.</p>
            )}
          </section>

          <section className="review-card">
            <div className="review-section-heading">
              <Icon name="forum" />
              <h3>Комментарии</h3>
            </div>
            <CommentThread
              comments={assignment?.comments ?? []}
              teacherId={assignment?.teacher_id}
              selfId={user?.user_id}
              onSubmit={async (text) => {
                await api.post(`/assignments/${assignmentId}/comments`, { text });
                detail.reload();
              }}
            />
          </section>
        </div>

        <div className="dashboard-column sticky-column">
          <ReviewPanel assignmentId={assignmentId} detail={detail} submission={submission} name={name} />
        </div>
      </div>
    </AppShell>
  );
}

function ReviewPanel({
  assignmentId,
  detail,
  submission,
  name,
}: {
  assignmentId: string;
  detail: Async<AssignmentDetail>;
  submission: Submission | null;
  name: string;
}) {
  const toast = useToast();
  const [verdict, setVerdict] = useState<Verdict | null>(null);
  const [comment, setComment] = useState("");
  const [busy, setBusy] = useState(false);
  const canSubmit = useMemo(() => !!verdict && (verdict !== "needs_fix" || comment.trim().length > 0), [comment, verdict]);

  async function submit() {
    if (!canSubmit) {
      toast({ tone: "danger", title: verdict ? "Нужен комментарий" : "Выберите результат", body: verdict ? "Опишите, что исправить" : "Принять или вернуть на правки" });
      return;
    }
    setBusy(true);
    try {
      await api.post(`/assignments/${assignmentId}/review`, {
        status: verdict,
        comment: comment.trim() || undefined,
      });
      toast(
        verdict === "needs_fix"
          ? { tone: "warning", title: "Отправлено на правки", body: `${name} получит комментарий` }
          : { tone: "success", title: "Работа принята", body: `${name} · статус «выполнено»` },
      );
      setComment("");
      setVerdict(null);
      detail.reload();
    } catch (err) {
      toast({ tone: "danger", title: "Не удалось сохранить", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  const quick = verdict === "needs_fix"
    ? ["Проверь задачу 5", "Ошибка в знаке", "Распиши шаги подробнее"]
    : ["Отлично, всё верно!", "Молодец", "Чисто оформлено"];

  return (
    <section className="review-verdict-panel">
      <h2>Проверка работы</h2>
      <p>Выберите результат и оставьте комментарий</p>
      {!submission && <p className="hint">Проверка станет доступна после отправки решения.</p>}

      <div className="review-options">
        <button type="button" className={verdict === "reviewed" ? "review-option active" : "review-option"} onClick={() => setVerdict("reviewed")}>
          <span><Icon name={verdict === "reviewed" ? "check_circle" : "check"} /></span>
          <strong>Принять работу</strong>
        </button>
        <button type="button" className={verdict === "needs_fix" ? "review-option warning active" : "review-option warning"} onClick={() => setVerdict("needs_fix")}>
          <span><Icon name={verdict === "needs_fix" ? "edit_note" : "edit"} /></span>
          <strong>Вернуть на правки</strong>
        </button>
      </div>

      <div className="field">
        <label>Комментарий {verdict === "needs_fix" ? "(обязательно)" : "(необязательно)"}
          <textarea
            value={comment}
            onChange={(e) => setComment(e.target.value)}
            placeholder={verdict === "needs_fix" ? "Что нужно исправить…" : "Похвалите или дайте совет…"}
          />
        </label>
      </div>

      <div className="quick-replies">
        {quick.map((text) => (
          <button type="button" className="small" key={text} onClick={() => setComment((cur) => (cur ? `${cur} ${text}` : text))}>
            {text}
          </button>
        ))}
      </div>

      <Button
        variant={verdict === "needs_fix" ? "secondary" : "primary"}
        className="review-submit"
        block
        icon={verdict === "needs_fix" ? "send" : "check"}
        loading={busy}
        disabled={!submission || !canSubmit}
        onClick={submit}
      >
        {verdict === "needs_fix" ? "Отправить на правки" : "Принять работу"}
      </Button>
      <div className="review-notify"><Icon name="notifications_active" />Ученик получит уведомление о результате</div>
    </section>
  );
}
