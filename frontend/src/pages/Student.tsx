import { useMemo } from "react";
import { Link } from "react-router-dom";
import {
  api,
  reports,
  type Assignment,
  type Lesson,
  type Receipt,
  type StudentDashboard,
  type StudentSummary,
} from "../api";
import {
  AppShell,
  Avatar,
  Card,
  Counter,
  EmptyState,
  ErrorState,
  Icon,
  ListRow,
  MessagesCard,
  NotificationsPanel,
  SkeletonRows,
  StatusPill,
  fmtDate,
  useAsync,
} from "../ui";
import { useAuth } from "../auth";
import { useDomainRefresh, useOnlineStatus } from "../realtime";
import { studentNav, money } from "./studentNav";

type Async<T> = ReturnType<typeof useAsync<T>>;

// ДЗ, которые ученику нужно сдать (активные).
const TO_SUBMIT = new Set(["assigned", "needs_fix", "in_progress"]);
// ДЗ, проверенные преподавателем.
const REVIEWED = new Set(["reviewed", "accepted", "done"]);

function sumActivity(summaries: StudentSummary[]) {
  return summaries.reduce(
    (acc, item) => ({
      upcoming: acc.upcoming + item.activity.upcoming_lessons_count,
      activeAssignments: acc.activeAssignments + item.activity.active_assignments_count,
    }),
    { upcoming: 0, activeAssignments: 0 },
  );
}

function currencyOf(dashboard: StudentDashboard | null): string {
  return dashboard?.summaries.find((s) => s.finance.currency)?.finance.currency ?? "RUB";
}

function dayLabel(iso?: string): string {
  if (!iso) return "—";
  const d = new Date(iso);
  if (isNaN(d.getTime())) return iso;
  const today = new Date();
  const tomorrow = new Date();
  tomorrow.setDate(today.getDate() + 1);
  const time = d.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" });
  if (d.toDateString() === today.toDateString()) return `Сегодня, ${time}`;
  if (d.toDateString() === tomorrow.toDateString()) return `Завтра, ${time}`;
  return d.toLocaleDateString("ru-RU", { day: "numeric", month: "long" }) + `, ${time}`;
}

export default function Student() {
  const { user } = useAuth();
  const dashboard = useAsync<StudentDashboard>(() => reports.studentDashboard(), []);
  const lessons = useAsync<Lesson[]>(() => api.get("/lessons"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const receipts = useAsync<Receipt[]>(() => api.get("/payments/receipts"), []);

  const report = dashboard.data;
  const currency = currencyOf(report);
  const activity = sumActivity(report?.summaries ?? []);

  function reloadAll() {
    dashboard.reload();
    lessons.reload();
    assignments.reload();
    receipts.reload();
  }

  useDomainRefresh(reloadAll, [""]);

  const scheduled = useMemo(
    () =>
      (lessons.data ?? [])
        .filter((l) => l.status === "scheduled")
        .sort((a, b) => new Date(a.starts_at).getTime() - new Date(b.starts_at).getTime()),
    [lessons.data],
  );
  const nextLesson = scheduled[0];

  const assignmentList = assignments.data ?? [];
  const toSubmit = assignmentList.filter((a) => TO_SUBMIT.has(a.status));
  const reviewed = assignmentList.filter((a) => REVIEWED.has(a.status));
  const activeAssignments = report ? activity.activeAssignments : toSubmit.length;

  const teacherContacts = useMemo(
    () =>
      (report?.summaries ?? [])
        .filter((s) => s.teacher_id)
        .map((s) => ({ id: s.teacher_id, name: s.teacher_name || "Преподаватель" })),
    [report?.summaries],
  );

  return (
    <AppShell
      title={greeting(user?.display_name)}
      subtitle={new Date().toLocaleDateString("ru-RU", { weekday: "long", day: "numeric", month: "long" })}
      navSection="Учёба"
      accent="student"
      navItems={studentNav("summary", {
        lessons: report ? activity.upcoming : scheduled.length,
        assignments: activeAssignments,
        receipts: report?.pending_receipts_count ?? 0,
      })}
      actions={
        <Link className="primary-action" to="/student/payments">
          <Icon name="upload_file" />
          <span>Загрузить чек</span>
        </Link>
      }
    >
      <div className="container">
        {/* Верхний ряд: баланс (read-only) + ближайшее занятие */}
        <div className="student-top">
          <BalanceCard report={report} currency={currency} loading={dashboard.loading} />
          <NextLessonCard lesson={nextLesson} loading={lessons.loading && !lessons.data} contacts={teacherContacts} />
        </div>

        <div className="grid">
          {/* LEFT */}
          <div className="dashboard-column">
            <ActiveAssignmentsCard assignments={assignments} toSubmit={toSubmit} />
            <ReviewedCard reviewed={reviewed} loading={assignments.loading && !assignments.data} />
          </div>

          {/* RIGHT */}
          <div className="dashboard-column">
            <MyReceiptsCard receipts={receipts} />
            <NotificationsPanel />
            <MessagesCard contacts={teacherContacts} chatHref="/student/chat" />
          </div>
        </div>
      </div>
    </AppShell>
  );
}

function greeting(name?: string): string {
  const first = (name ?? "").trim().split(/\s+/)[0];
  return first ? `Привет, ${first}!` : "Главная";
}

/* ============ Баланс (только просмотр) ============ */

function BalanceCard({
  report,
  currency,
  loading,
}: {
  report: StudentDashboard | null;
  currency: string;
  loading: boolean;
}) {
  const debt = report?.total_debt_amount ?? 0;
  const overpaid = report?.total_overpaid_amount ?? 0;

  let value = money(0, currency);
  let caption = "нет задолженности";
  if (debt > 0) {
    value = money(debt, currency);
    caption = "к оплате преподавателю";
  } else if (overpaid > 0) {
    value = money(overpaid, currency);
    caption = "переплата · зачтётся в счёт занятий";
  }

  return (
    <div className="balance-hero">
      <div className="label">
        <Icon name="account_balance_wallet" />
        <span>Мой баланс</span>
        <span className="balance-lock"><Icon name="lock" />только просмотр</span>
      </div>
      <div className="value">{loading && !report ? "…" : value}</div>
      <div className="hint">{caption}</div>
      <div className="balance-foot">
        <div>
          <div className="balance-foot-label">Чеки на проверке</div>
          <div className="balance-foot-value">{report?.pending_receipts_count ?? 0}</div>
        </div>
        <div>
          <div className="balance-foot-label">На сумму</div>
          <div className="balance-foot-value">{money(report?.pending_receipts_amount, currency)}</div>
        </div>
      </div>
    </div>
  );
}

/* ============ Ближайшее занятие ============ */

function NextLessonCard({
  lesson,
  loading,
  contacts,
}: {
  lesson?: Lesson;
  loading: boolean;
  contacts: { id: string; name: string }[];
}) {
  const teacherName = lesson ? contacts.find((c) => c.id === lesson.teacher_id)?.name ?? "Преподаватель" : "";
  const online = useOnlineStatus(lesson?.teacher_id);

  return (
    <section className="next-lesson">
      <div className="next-lesson-head">
        <Icon name="event_upcoming" />
        <span>Ближайшее занятие</span>
        {lesson && <span style={{ marginLeft: "auto" }}><StatusPill status={lesson.status} /></span>}
      </div>
      {loading ? (
        <div style={{ padding: "8px 0" }}><SkeletonRows count={1} /></div>
      ) : !lesson ? (
        <EmptyState icon="event_busy" title="Занятий не запланировано" hint="Новые занятия появятся здесь." />
      ) : (
        <>
          <div className="next-lesson-when">{dayLabel(lesson.starts_at)}</div>
          <div className="next-lesson-topic">{lesson.topic || "Занятие"}</div>
          <div className="next-lesson-foot">
            <Avatar name={teacherName} tone="teacher" presence={online ? "online" : undefined} />
            <div className="dash-main">
              <div className="t">{teacherName}</div>
              <div className={"s" + (online ? " online-text" : "")}>{online ? "онлайн" : "не в сети"}</div>
            </div>
            <Link className="button-like small has-icon" to="/student/chat">
              <Icon name="chat_bubble" />Написать
            </Link>
          </div>
        </>
      )}
    </section>
  );
}

/* ============ Активные задания ============ */

function ActiveAssignmentsCard({
  assignments,
  toSubmit,
}: {
  assignments: Async<Assignment[]>;
  toSubmit: Assignment[];
}) {
  return (
    <Card
      title="Активные задания"
      icon="assignment"
      actions={
        <>
          {toSubmit.length > 0 && <span className="card-head-chip warning">{toSubmit.length} ждут сдачи</span>}
          <Link className="card-head-link" to="/student/assignments">Все задания</Link>
        </>
      }
    >
      {assignments.loading && !assignments.data ? (
        <SkeletonRows count={2} />
      ) : assignments.error ? (
        <ErrorState error={assignments.error} onRetry={assignments.reload} />
      ) : toSubmit.length === 0 ? (
        <EmptyState tone="success" icon="task_alt" title="Нет заданий к сдаче" hint="Все домашние работы сданы." />
      ) : (
        toSubmit.map((a) => <AssignmentRow key={a.id} assignment={a} />)
      )}
    </Card>
  );
}

function AssignmentRow({ assignment }: { assignment: Assignment }) {
  const needsFix = assignment.status === "needs_fix";
  return (
    <ListRow
      leading={
        <span className={"dash-icon lg " + (needsFix ? "tone-danger" : "tone-info")}>
          <Icon name={needsFix ? "edit_note" : "description"} />
        </span>
      }
      title={
        <span className="assignment-title-line">
          {assignment.title}
          <StatusPill status={assignment.status} />
        </span>
      }
      subtitle={assignment.due_at ? `Дедлайн ${fmtDate(assignment.due_at)}` : "Без срока"}
    >
      <Link className="button-like primary small" to="/student/assignments">
        {needsFix ? "Сдать заново" : "Сдать решение"}
      </Link>
    </ListRow>
  );
}

/* ============ Недавно проверено ============ */

function ReviewedCard({ reviewed, loading }: { reviewed: Assignment[]; loading: boolean }) {
  return (
    <Card title="Недавно проверено" icon="grading">
      {loading ? (
        <SkeletonRows count={2} />
      ) : reviewed.length === 0 ? (
        <EmptyState icon="grading" title="Пока нет проверенных работ" />
      ) : (
        reviewed.slice(0, 4).map((a) => (
          <ListRow
            key={a.id}
            leading={<span className="dash-icon lg tone-success"><Icon name="task_alt" /></span>}
            title={a.title}
            subtitle={a.due_at ? `Срок был ${fmtDate(a.due_at)}` : "Проверено преподавателем"}
          >
            <StatusPill status={a.status} />
          </ListRow>
        ))
      )}
    </Card>
  );
}

/* ============ Мои чеки ============ */

const RECEIPT_ICON: Record<string, { icon: string; tone: string }> = {
  pending_review: { icon: "hourglass_top", tone: "tone-amber" },
  pending: { icon: "hourglass_top", tone: "tone-amber" },
  confirmed: { icon: "check_circle", tone: "tone-success" },
  rejected: { icon: "cancel", tone: "tone-danger" },
};

function MyReceiptsCard({ receipts }: { receipts: Async<Receipt[]> }) {
  const list = (receipts.data ?? [])
    .slice()
    .sort((a, b) => new Date(b.submitted_at ?? 0).getTime() - new Date(a.submitted_at ?? 0).getTime());
  const pending = list.filter((r) => r.status === "pending_review" || r.status === "pending").length;

  return (
    <Card
      title="Мои чеки"
      icon="receipt_long"
      actions={
        <>
          {pending > 0 && <Counter value={pending} tone="warning" />}
          <Link className="card-head-link" to="/student/receipts">Все</Link>
        </>
      }
    >
      {receipts.loading && !receipts.data ? (
        <SkeletonRows count={3} />
      ) : receipts.error ? (
        <ErrorState error={receipts.error} onRetry={receipts.reload} />
      ) : list.length === 0 ? (
        <EmptyState icon="receipt_long" title="Чеков пока нет" hint="Загрузите чек об оплате на странице «Оплата»." />
      ) : (
        <div className="card-scroll">
          {list.slice(0, 8).map((r) => {
            const meta = RECEIPT_ICON[r.status] ?? { icon: "receipt_long", tone: "tone-muted" };
            return (
              <ListRow
                key={r.id}
                leading={<span className={"dash-icon " + meta.tone}><Icon name={meta.icon} /></span>}
                title={money(r.amount, r.currency)}
                subtitle={fmtDate(r.submitted_at) || "—"}
              >
                <StatusPill status={r.status} />
              </ListRow>
            );
          })}
        </div>
      )}
    </Card>
  );
}
