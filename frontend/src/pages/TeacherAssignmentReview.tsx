import { useMemo, useState, type FormEvent } from "react";
import { Link, Navigate, useParams } from "react-router-dom";
import {
  api,
  type AssignmentDetail,
  type StudentLink,
  type Submission,
  type TeacherDashboard,
} from "../api";
import { AppShell, ErrorMsg, FileChips, Icon, Notice, StatusPill, fmtDate, useAsync } from "../ui";
import { initials, teacherNav } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;
type Verdict = "reviewed" | "needs_fix";

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
  const detail = useAsync<AssignmentDetail>(() => api.get(`/assignments/${assignmentId}`), [assignmentId]);

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
      actions={<Link className="button-link" to="/teacher/assignments"><Icon name="arrow_back" />Домашние задания</Link>}
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
            {detail.loading && !assignment && <p className="hint">Загрузка...</p>}
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
                  <div className="avatar">{initials(name)}</div>
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
              <Icon name="history" />
              <h3>История сдач</h3>
            </div>
            <div className="review-timeline">
              {submission && <TimelineItem tone="warning" title="Сдано на проверку" meta={fmtDate(submission.submitted_at) || "дата не указана"} />}
              {(assignment?.comments ?? []).map((comment) => (
                <TimelineItem tone="danger" title="Комментарий преподавателя" meta={`${fmtDate(comment.created_at)} · ${comment.text}`} key={comment.id} />
              ))}
              {assignment && <TimelineItem tone="info" title="Выдано задание" meta={fmtDate(assignment.created_at) || "дата не указана"} last />}
              {!assignment && !detail.loading && <p className="hint">Задание не найдено.</p>}
            </div>
          </section>
        </div>

        <div className="dashboard-column sticky-column">
          <ReviewPanel assignmentId={assignmentId} detail={detail} submission={submission} />
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
  const [verdict, setVerdict] = useState<Verdict | null>(null);
  const [comment, setComment] = useState("");
  const [reviewFile, setReviewFile] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const canSubmit = useMemo(() => !!verdict && (verdict !== "needs_fix" || comment.trim().length > 0), [comment, verdict]);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setError(null);
    setNotice(null);
    if (!canSubmit) {
      setError(verdict ? "Для возврата на правки нужен комментарий." : "Выберите результат проверки.");
      return;
    }
    setBusy(true);
    try {
      await api.post(`/assignments/${assignmentId}/review`, {
        status: verdict,
        comment: comment.trim() || undefined,
      });
      setNotice(verdict === "needs_fix" ? "Работа возвращена на правки." : "Работа принята.");
      setComment("");
      detail.reload();
    } catch (err) {
      setError((err as Error).message);
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
      <ErrorMsg error={error} />
      <Notice text={notice} />
      {!submission && <p className="hint">Проверка станет доступна после отправки решения.</p>}

      <form onSubmit={submit}>
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
              onChange={(event) => setComment(event.target.value)}
              placeholder={verdict === "needs_fix" ? "Что нужно исправить..." : "Похвалите или дайте совет..."}
            />
          </label>
        </div>

        <div className="quick-replies">
          {quick.map((text) => (
            <button type="button" className="small" key={text} onClick={() => setComment((current) => (current ? `${current} ${text}` : text))}>
              {text}
            </button>
          ))}
        </div>

        <button type="button" className={"review-attach" + (reviewFile ? " active" : "")} onClick={() => setReviewFile((value) => !value)}>
          <Icon name={reviewFile ? "check_circle" : "attach_file"} />
          {reviewFile ? "разметка_проверка.pdf · 210 КБ" : "Прикрепить файл"}
        </button>

        <button className="primary review-submit" type="submit" disabled={busy || !submission || !canSubmit}>
          <Icon name={verdict === "reviewed" ? "check" : "send"} />
          {busy ? "Сохранение..." : verdict === "needs_fix" ? "Отправить на правки" : "Принять работу"}
        </button>
        <div className="review-notify"><Icon name="notifications_active" />Ученик получит уведомление о результате</div>
      </form>
    </section>
  );
}

function TimelineItem({ tone, title, meta, last }: { tone: "warning" | "danger" | "info"; title: string; meta: string; last?: boolean }) {
  return (
    <div className="timeline-item">
      <div className="timeline-marker">
        <span className={"timeline-dot " + tone}></span>
        {!last && <i></i>}
      </div>
      <div>
        <strong>{title}</strong>
        <span>{meta}</span>
      </div>
    </div>
  );
}
