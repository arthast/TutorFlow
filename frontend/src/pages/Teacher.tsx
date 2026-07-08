import { useMemo, useState } from "react";
import { Link } from "react-router-dom";
import {
  api,
  openFile,
  reports,
  type Assignment,
  type CompleteLessonResponse,
  type Lesson,
  type Receipt,
  type StudentLink,
  type TeacherDashboard,
} from "../api";
import {
  AppShell,
  Avatar,
  Button,
  Card,
  Counter,
  EmptyState,
  ErrorState,
  Field,
  Icon,
  Input,
  ListRow,
  MessagesCard,
  Metric,
  Modal,
  NotificationsPanel,
  SkeletonRows,
  StatusPill,
  fmtDate,
  useAsync,
  useToast,
} from "../ui";
import { useDomainRefresh, useOnlineStatus } from "../realtime";
import { teacherNav, money, timeOnly } from "./teacherNav";

type Async<T> = ReturnType<typeof useAsync<T>>;

const TONES = ["teacher", "student", "amber", "muted"] as const;
type Tone = (typeof TONES)[number];
function toneFor(index: number): Tone {
  return TONES[index % TONES.length];
}

function studentName(students: StudentLink[], studentId: string): string {
  return students.find((s) => s.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}

function isToday(iso?: string): boolean {
  if (!iso) return false;
  const d = new Date(iso);
  return !isNaN(d.getTime()) && d.toDateString() === new Date().toDateString();
}

export default function Teacher() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts?status=pending_review"), []);

  const report = dashboard.data;
  const studentList = students.data ?? [];

  function reloadAll() {
    dashboard.reload();
    lessons.reload();
    receipts.reload();
    assignments.reload();
  }

  // Realtime: обновляем сводку, когда прилетают доменные уведомления.
  useDomainRefresh(reloadAll, [""]);

  const todayLessons = useMemo(
    () =>
      (lessons.data ?? [])
        .filter((l) => l.status === "scheduled" && isToday(l.starts_at))
        .sort((a, b) => new Date(a.starts_at).getTime() - new Date(b.starts_at).getTime()),
    [lessons.data],
  );
  const scheduledCount = (lessons.data ?? []).filter((l) => l.status === "scheduled").length;
  const submittedAssignments = (assignments.data ?? []).filter((a) => a.status === "submitted");
  const pendingReceipts = receipts.data ?? [];
  const activeCount = studentList.filter((s) => s.status === "active").length;

  const receiptCount = report?.pending_receipts_count ?? pendingReceipts.length;
  const receiptSum = report?.pending_receipts_amount ?? pendingReceipts.reduce((a, r) => a + r.amount, 0);
  const overpaid = report?.total_overpaid_amount ?? 0;

  return (
    <AppShell
      title="Сводка"
      subtitle={new Date().toLocaleDateString("ru-RU", { weekday: "long", day: "numeric", month: "long" })}
      navSection="Работа"
      navItems={teacherNav("summary", {
        students: report?.students_count ?? studentList.length,
        lessons: report?.upcoming_lessons_count ?? scheduledCount,
        assignments: report?.pending_submissions_count ?? submittedAssignments.length,
        receipts: receiptCount,
      })}
      actions={
        <Link className="primary-action" to="/teacher/lessons">
          <Icon name="add" />
          <span>Новое занятие</span>
        </Link>
      }
    >
      <div className="container">
        {/* KPI */}
        <div className="metrics">
          <Metric
            icon="group"
            label="Ученики"
            value={report?.students_count ?? studentList.length}
            sub={`активных: ${activeCount}`}
          />
          <Metric
            icon="calendar_month"
            label="Ближайшие занятия"
            value={report?.upcoming_lessons_count ?? scheduledCount}
            sub={`сегодня: ${todayLessons.length} впереди`}
          />
          <Metric
            icon="receipt_long"
            label="Чеки на проверку"
            value={receiptCount}
            tone="warn"
            pill={{ label: "ждут", tone: "warning" }}
            sub={`на сумму ${money(receiptSum)}`}
          />
          <Metric
            icon="account_balance_wallet"
            label="Долг учеников"
            value={money(report?.total_debt_amount)}
            sub={`переплат: ${money(overpaid)} · должников: ${report?.students_with_debt_count ?? 0}`}
            subTone={overpaid > 0 ? "pos" : undefined}
          />
        </div>

        <div className="grid">
          {/* LEFT */}
          <div className="dashboard-column">
            <TodayCard
              lessons={lessons}
              todayLessons={todayLessons}
              students={studentList}
              onChanged={reloadAll}
            />
            <AttentionCard
              receipts={receipts}
              submitted={submittedAssignments}
              students={studentList}
              loading={receipts.loading || assignments.loading}
              onChanged={reloadAll}
            />
          </div>

          {/* RIGHT */}
          <div className="dashboard-column">
            <NotificationsPanel />
            <MessagesCard
              contacts={studentList.map((s) => ({ id: s.student_id, name: s.display_name }))}
              chatHref="/teacher/chat"
            />
          </div>
        </div>
      </div>
    </AppShell>
  );
}

/* ============ Сегодня ============ */

function TodayCard({
  lessons,
  todayLessons,
  students,
  onChanged,
}: {
  lessons: Async<Lesson[]>;
  todayLessons: Lesson[];
  students: StudentLink[];
  onChanged: () => void;
}) {
  return (
    <Card
      title="Сегодня"
      icon="today"
      actions={
        <>
          <span className="card-head-chip">{todayLessons.length} занятия</span>
          <Link className="card-head-link" to="/teacher/lessons">Всё расписание</Link>
        </>
      }
    >
      {lessons.loading && !lessons.data ? (
        <SkeletonRows count={3} />
      ) : lessons.error ? (
        <ErrorState error={lessons.error} onRetry={lessons.reload} />
      ) : todayLessons.length === 0 ? (
        <EmptyState icon="event_available" title="На сегодня занятий нет" hint="Запланируйте занятие на странице «Занятия»." />
      ) : (
        todayLessons.map((lesson, i) => (
          <TodayLessonRow
            key={lesson.id}
            lesson={lesson}
            tone={toneFor(i)}
            name={studentName(students, lesson.student_id)}
            onChanged={onChanged}
          />
        ))
      )}
    </Card>
  );
}

function isoToLocalInput(iso: string): string {
  const date = new Date(iso);
  if (isNaN(date.getTime())) return "";
  const local = new Date(date.getTime() - date.getTimezoneOffset() * 60000);
  return local.toISOString().slice(0, 16);
}

function TodayLessonRow({
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
  const [rescheduleOpen, setRescheduleOpen] = useState(false);

  async function complete() {
    setBusy(true);
    try {
      const result = await api.post<CompleteLessonResponse>(`/lessons/${lesson.id}/complete`);
      toast({
        tone: "success",
        title: "Занятие завершено",
        body: result.charge_status === "pending" ? `${name} · начисление создаётся` : name,
      });
      onChanged();
    } catch (err) {
      toast({ tone: "danger", title: "Не удалось завершить", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <ListRow
      time={timeOnly(lesson.starts_at)}
      leading={<Avatar name={name} tone={tone} presence={online ? "online" : undefined} />}
      title={name}
      subtitle={lesson.topic || "Занятие"}
    >
      <StatusPill status={lesson.status} />
      <Button variant="primary" size="sm" loading={busy} onClick={complete}>Завершить</Button>
      <button className="icon-button small" type="button" title="Перенести" aria-label="Перенести занятие" onClick={() => setRescheduleOpen(true)}>
        <Icon name="schedule" />
      </button>
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
    </ListRow>
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
  const [starts, setStarts] = useState(isoToLocalInput(lesson.starts_at));
  const [ends, setEnds] = useState(isoToLocalInput(lesson.ends_at));
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit() {
    setBusy(true);
    setError(null);
    try {
      await api.post(`/lessons/${lesson.id}/reschedule`, {
        new_starts_at: new Date(starts).toISOString(),
        new_ends_at: new Date(ends).toISOString(),
      });
      toast({ tone: "info", title: "Занятие перенесено", body: name });
      onDone();
    } catch (err) {
      setError((err as Error).message);
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
          <Button variant="primary" type="submit" icon="schedule" loading={busy}>Перенести</Button>
        </>
      }
    >
      <div className="modal-fields">
        <Field label="Новое начало" error={error}>
          <Input type="datetime-local" value={starts} onChange={(e) => setStarts(e.target.value)} required />
        </Field>
        <Field label="Новый конец">
          <Input type="datetime-local" value={ends} onChange={(e) => setEnds(e.target.value)} required />
        </Field>
      </div>
    </Modal>
  );
}

/* ============ Требует внимания ============ */

function AttentionCard({
  receipts,
  submitted,
  students,
  loading,
  onChanged,
}: {
  receipts: Async<Receipt[]>;
  submitted: Assignment[];
  students: StudentLink[];
  loading: boolean;
  onChanged: () => void;
}) {
  const list = receipts.data ?? [];
  return (
    <Card title="Требует внимания" icon="pending_actions">
      <div className="dash-head">
        <span className="lbl">Чеки на проверку</span>
        <Counter value={list.length} tone="warning" />
      </div>
      {loading && !receipts.data ? (
        <SkeletonRows count={2} />
      ) : receipts.error ? (
        <ErrorState error={receipts.error} onRetry={receipts.reload} />
      ) : list.length === 0 ? (
        <EmptyState tone="success" icon="task_alt" title="Все чеки обработаны" />
      ) : (
        list.map((receipt, i) => (
          <ReceiptRow
            key={receipt.id}
            receipt={receipt}
            tone={toneFor(i)}
            name={studentName(students, receipt.student_id)}
            onChanged={onChanged}
          />
        ))
      )}

      <div className="dash-head">
        <span className="lbl">Домашние работы на проверку</span>
        <Counter value={submitted.length} tone="muted" />
      </div>
      {submitted.length === 0 ? (
        <EmptyState icon="assignment_turned_in" title="Нет работ на проверку" />
      ) : (
        submitted.map((assignment, i) => (
          <ListRow
            key={assignment.id}
            leading={<Avatar name={studentName(students, assignment.student_id)} tone={toneFor(i)} />}
            title={studentName(students, assignment.student_id)}
            subtitle={assignment.title}
          >
            <StatusPill status={assignment.status} />
            <Link className="button-like secondary small" to={`/teacher/assignments/${assignment.id}/review`}>
              Проверить
            </Link>
          </ListRow>
        ))
      )}
    </Card>
  );
}

function ReceiptRow({
  receipt,
  tone,
  name,
  onChanged,
}: {
  receipt: Receipt;
  tone: Tone;
  name: string;
  onChanged: () => void;
}) {
  const toast = useToast();
  const [busy, setBusy] = useState(false);

  async function act(path: string, body: unknown, ok: { tone: "success" | "warning"; title: string; body: string }) {
    setBusy(true);
    try {
      await api.post(path, body);
      toast({ tone: ok.tone, title: ok.title, body: ok.body });
      onChanged();
    } catch (err) {
      toast({ tone: "danger", title: "Ошибка", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  function confirm() {
    act(`/payments/receipts/${receipt.id}/confirm`, undefined, {
      tone: "success",
      title: "Чек подтверждён",
      body: `${name} · долг обновлён`,
    });
  }

  function reject() {
    if (!window.confirm(`Отклонить чек ученика ${name} на ${money(receipt.amount, receipt.currency)}?`)) return;
    const reason = window.prompt("Причина отклонения (необязательно):", "") ?? "";
    act(`/payments/receipts/${receipt.id}/reject`, { comment: reason }, {
      tone: "warning",
      title: "Чек отклонён",
      body: `${name} получит уведомление`,
    });
  }

  return (
    <ListRow
      leading={<Avatar name={name} tone={tone} size="sm" />}
      title={name}
      subtitle={fmtDate(receipt.submitted_at) || "чек на оплату"}
    >
      <span className="dash-amount">{money(receipt.amount, receipt.currency)}</span>
      <button
        className="icon-button small"
        type="button"
        title="Открыть чек"
        aria-label="Открыть файл чека"
        onClick={() => openFile(receipt.file_id).catch((e) => toast({ tone: "danger", title: "Ошибка", body: (e as Error).message }))}
      >
        <Icon name="visibility" />
      </button>
      <Button variant="primary" size="sm" loading={busy} onClick={confirm}>
        Подтвердить
      </Button>
      <Button variant="danger" size="sm" disabled={busy} onClick={reject}>Отклонить</Button>
    </ListRow>
  );
}
