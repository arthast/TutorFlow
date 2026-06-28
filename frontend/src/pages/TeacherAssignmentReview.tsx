import { useMemo, useState, type FormEvent } from "react";
import { Link, Navigate, useParams } from "react-router-dom";
import {
  api,
  type AssignmentDetail,
  type StudentLink,
  type Submission,
  type TeacherDashboard,
} from "../api";
import { AppShell, Card, ErrorMsg, FileChips, Icon, ListState, Notice, StatusPill, fmtDate, useAsync } from "../ui";
import { initials, teacherNav } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;

function latestSubmission(submissions?: Submission[]): Submission | null {
  if (!submissions || submissions.length === 0) return null;
  return [...submissions].sort((a, b) => new Date(b.submitted_at ?? "").getTime() - new Date(a.submitted_at ?? "").getTime())[0];
}

function studentName(students: StudentLink[], studentId?: string): string {
  if (!studentId) return "Ученик";
  return students.find((student) => student.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}

export default function TeacherAssignmentReview() {
  const { assignmentId } = useParams();
  const dashboard = useAsync<TeacherDashboard>(() => api.get("/dashboard/teacher"), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const detail = useAsync<AssignmentDetail>(
    () => api.get(`/assignments/${assignmentId}`),
    [assignmentId],
  );

  if (!assignmentId) return <Navigate to="/teacher" replace />;

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
    >
      <div className="review-layout">
        <div className="dashboard-column">
          <Card title={assignment?.title ?? "Домашнее задание"} icon="assignment">
            {detail.loading && !assignment && <p className="hint">Загрузка…</p>}
            <ErrorMsg error={detail.error} />
            {assignment && (
              <>
                <div className="profile-inline">
                  <div className="avatar">{initials(name)}</div>
                  <div>
                    <div className="summary-title">{name}</div>
                    <div className="muted">Статус задания: <StatusPill status={assignment.status} /></div>
                  </div>
                </div>
                {assignment.description && <p className="review-text">{assignment.description}</p>}
                <FileChips fileIds={assignment.file_ids} label="Материалы задания" />
              </>
            )}
          </Card>

          <Card title="Решение ученика" icon="upload_file">
            {submission ? (
              <>
                <div className="summary-grid summary-grid-wide">
                  <span>Статус: <StatusPill status={submission.status} /></span>
                  <span>Сдано: {fmtDate(submission.submitted_at) || "—"}</span>
                </div>
                {submission.text_answer && <p className="review-text">{submission.text_answer}</p>}
                <FileChips fileIds={submission.file_ids} label="Файлы решения" />
              </>
            ) : (
              <p className="hint">Ученик ещё не отправил решение.</p>
            )}
          </Card>

          <Card title="Комментарии" icon="forum">
            {(assignment?.comments ?? []).map((comment) => (
              <div className="row" key={comment.id}>
                <span>{comment.text}</span>
                <span className="hint">{fmtDate(comment.created_at)}</span>
              </div>
            ))}
            {assignment && (assignment.comments ?? []).length === 0 && <p className="hint">Комментариев пока нет.</p>}
            <ListState query={{ ...detail, data: assignment ? [assignment] : null }} empty="Задание не найдено." />
          </Card>
        </div>

        <div className="dashboard-column sticky-column">
          <ReviewPanel assignmentId={assignmentId} detail={detail} submission={submission} />
          <Card title="Навигация" icon="arrow_back">
            <Link className="button-link" to="/teacher">Вернуться в кабинет</Link>
          </Card>
        </div>
      </div>
    </AppShell>
  );
}

function ReviewPanel({
  assignmentId,
  detail,
  submission,
}: {
  assignmentId: string;
  detail: Async<AssignmentDetail>;
  submission: Submission | null;
}) {
  const [status, setStatus] = useState<"reviewed" | "needs_fix" | "accepted">("reviewed");
  const [comment, setComment] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const canSubmit = useMemo(
    () => status !== "needs_fix" || comment.trim().length > 0,
    [comment, status],
  );

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError(null);
    setNotice(null);
    if (!canSubmit) {
      setError("Для возврата на правки нужен комментарий.");
      return;
    }
    setBusy(true);
    try {
      await api.post(`/assignments/${assignmentId}/review`, {
        status,
        comment: comment.trim() || undefined,
      });
      setNotice(status === "needs_fix" ? "Работа возвращена на правки." : "Проверка сохранена.");
      setComment("");
      detail.reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Результат проверки" icon="grading">
      <ErrorMsg error={error} />
      <Notice text={notice} />
      {!submission && <p className="hint">Проверка станет доступна после отправки решения.</p>}
      <form onSubmit={submit}>
        <div className="review-options">
          <button type="button" className={status === "reviewed" ? "review-option active" : "review-option"} onClick={() => setStatus("reviewed")}>
            <Icon name="check_circle" />
            Принять работу
          </button>
          <button type="button" className={status === "needs_fix" ? "review-option active warning" : "review-option warning"} onClick={() => setStatus("needs_fix")}>
            <Icon name="edit_note" />
            Вернуть на правки
          </button>
          <button type="button" className={status === "accepted" ? "review-option active" : "review-option"} onClick={() => setStatus("accepted")}>
            <Icon name="task_alt" />
            Зачесть окончательно
          </button>
        </div>
        <div className="field">
          <label>Комментарий
            <textarea
              value={comment}
              onChange={(event) => setComment(event.target.value)}
              placeholder={status === "needs_fix" ? "Что нужно исправить…" : "Комментарий для ученика"}
            />
          </label>
        </div>
        <div className="quick-replies">
          {["Отлично, всё верно!", "Проверь задачу 5", "Распиши шаги подробнее"].map((text) => (
            <button type="button" className="small" key={text} onClick={() => setComment((current) => (current ? `${current} ${text}` : text))}>
              {text}
            </button>
          ))}
        </div>
        <button className="primary" type="submit" disabled={busy || !submission || !canSubmit}>
          {busy ? "Сохранение…" : "Отправить результат"}
        </button>
      </form>
    </Card>
  );
}
