import { useMemo, useRef, useState } from "react";
import { Link } from "react-router-dom";
import {
  api,
  reports,
  type Assignment,
  type FileMeta,
  type StudentLink,
  type TeacherDashboard,
} from "../api";
import {
  AppShell,
  Button,
  EmptyState,
  ErrorMsg,
  ErrorState,
  Field,
  Icon,
  Modal,
  Select,
  SkeletonRows,
  StatusPill,
  Tabs,
  useAsync,
  useToast,
  type TabItem,
} from "../ui";
import { useRealtimeEvent } from "../realtime";
import { teacherNav } from "./teacherNav";

type AssignmentTab = "all" | "review" | "active" | "done";

const ASSIGNMENT_STATUS: Record<string, {
  category: Exclude<AssignmentTab, "all">;
  accent: string;
  icon: string;
  iconClass: string;
}> = {
  assigned: { category: "active", accent: "#3b5bdb", icon: "assignment", iconClass: "assignment-icon-info" },
  submitted: { category: "review", accent: "#d99413", icon: "assignment_turned_in", iconClass: "assignment-icon-warning" },
  needs_fix: { category: "active", accent: "#d99413", icon: "edit_note", iconClass: "assignment-icon-warning" },
  expired: { category: "active", accent: "#e0584f", icon: "event_busy", iconClass: "assignment-icon-danger" },
  reviewed: { category: "done", accent: "#18a866", icon: "task_alt", iconClass: "assignment-icon-success" },
  accepted: { category: "done", accent: "#18a866", icon: "task_alt", iconClass: "assignment-icon-success" },
  done: { category: "done", accent: "#18a866", icon: "task_alt", iconClass: "assignment-icon-success" },
};

async function uploadAll(files: File[], purpose: string): Promise<string[]> {
  const ids: string[] = [];
  for (const file of files) {
    const form = new FormData();
    form.append("file", file);
    form.append("purpose", purpose);
    const meta = await api.upload<FileMeta>("/files", form);
    ids.push(meta.id);
  }
  return ids;
}

function statusMeta(status: string) {
  return ASSIGNMENT_STATUS[status] ?? ASSIGNMENT_STATUS.assigned;
}
function studentName(students: StudentLink[], studentId: string): string {
  return students.find((s) => s.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}
function initials(name: string): string {
  return name.split(/\s+/).filter(Boolean).slice(0, 2).map((p) => p[0]?.toUpperCase()).join("") || "??";
}
function deadlineLabel(a: Assignment): string {
  if (a.due_at) {
    const date = new Date(a.due_at);
    if (!isNaN(date.getTime())) return date.toLocaleString("ru-RU", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
  }
  return a.created_at ? `создано ${new Date(a.created_at).toLocaleDateString("ru-RU")}` : "без дедлайна";
}
function fileSize(file: File): string {
  if (file.size >= 1024 * 1024) return `${(file.size / (1024 * 1024)).toFixed(1)} МБ`;
  return `${Math.max(1, Math.round(file.size / 1024))} КБ`;
}

export default function TeacherAssignments() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const [tab, setTab] = useState<AssignmentTab>("all");
  const [query, setQuery] = useState("");
  const [createOpen, setCreateOpen] = useState(false);

  const studentList = students.data ?? [];

  useRealtimeEvent((event) => {
    if (["assignment", "submission", "review"].some((t) => event.type.startsWith(t))) {
      assignments.reload();
      dashboard.reload();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const counts = useMemo(() => {
    const base: Record<AssignmentTab, number> = { all: 0, review: 0, active: 0, done: 0 };
    (assignments.data ?? []).forEach((a) => {
      base.all += 1;
      base[statusMeta(a.status).category] += 1;
    });
    return base;
  }, [assignments.data]);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return (assignments.data ?? []).filter((a) => {
      const name = studentName(studentList, a.student_id).toLowerCase();
      const matchesQuery = !q || a.title.toLowerCase().includes(q) || name.includes(q);
      const matchesTab = tab === "all" || statusMeta(a.status).category === tab;
      return matchesQuery && matchesTab;
    });
  }, [assignments.data, query, studentList, tab]);

  const tabs: TabItem[] = [
    { key: "all", label: "Все", count: counts.all },
    { key: "review", label: "На проверку", count: counts.review },
    { key: "active", label: "В работе", count: counts.active },
    { key: "done", label: "Выполнено", count: counts.done },
  ];

  return (
    <AppShell
      title="Домашние задания"
      subtitle="Выдача и проверка работ"
      navSection="Работа"
      navItems={teacherNav("assignments", {
        students: dashboard.data?.students_count,
        lessons: dashboard.data?.upcoming_lessons_count,
        assignments: dashboard.data?.pending_submissions_count,
        receipts: dashboard.data?.pending_receipts_count,
      })}
      actions={<Button variant="primary" icon="add" onClick={() => setCreateOpen(true)}>Выдать ДЗ</Button>}
    >
      <div className="container">
        <Tabs
          items={tabs}
          active={tab}
          onChange={(k) => setTab(k as AssignmentTab)}
          right={
            <div className="search-field">
              <Icon name="search" />
              <input placeholder="Поиск по теме или ученику…" aria-label="Поиск по теме или ученику" value={query} onChange={(e) => setQuery(e.target.value)} />
            </div>
          }
        />

        {assignments.loading && !assignments.data ? (
          <div className="card"><SkeletonRows count={4} /></div>
        ) : assignments.error ? (
          <ErrorState error={assignments.error} onRetry={assignments.reload} />
        ) : filtered.length === 0 ? (
          <EmptyState icon="assignment" title="Заданий нет" hint="В этой вкладке пока пусто." />
        ) : (
          <div className="assignment-list">
            {filtered.map((a) => (
              <AssignmentRow assignment={a} students={studentList} key={a.id} />
            ))}
          </div>
        )}
      </div>

      {createOpen && (
        <CreateAssignmentModal
          students={studentList}
          onClose={() => setCreateOpen(false)}
          onCreated={() => {
            assignments.reload();
            dashboard.reload();
            setCreateOpen(false);
          }}
        />
      )}
    </AppShell>
  );
}

function AssignmentRow({ assignment, students }: { assignment: Assignment; students: StudentLink[] }) {
  const meta = statusMeta(assignment.status);
  const name = studentName(students, assignment.student_id);
  const review = meta.category === "review";
  const expired = assignment.status === "expired";
  return (
    <div className="assignment-list-row" style={{ borderLeftColor: meta.accent }}>
      <div className={"assignment-row-icon " + meta.iconClass}>
        <Icon name={meta.icon} />
      </div>
      <div className="assignment-row-main">
        <div className="assignment-row-title">
          <Link to={`/teacher/assignments/${assignment.id}/review`}>{assignment.title}</Link>
          {!!assignment.file_ids?.length && (
            <span className="file-count"><Icon name="attach_file" />{assignment.file_ids.length}</span>
          )}
        </div>
        <div className="assignment-row-meta">
          <span><span className="mini-avatar">{initials(name)}</span>{name}</span>
          <span className={expired ? "deadline danger" : assignment.due_at ? "deadline" : ""}>
            <Icon name="schedule" />{deadlineLabel(assignment)}
          </span>
        </div>
      </div>
      <StatusPill status={assignment.status} />
      <Link
        className={"button-like small " + (review ? "primary" : "secondary")}
        to={`/teacher/assignments/${assignment.id}/review`}
      >
        {review ? "Проверить" : assignment.status === "assigned" ? "Подробнее" : "Открыть"}
      </Link>
    </div>
  );
}

function CreateAssignmentModal({
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
  const [title, setTitle] = useState("");
  const [description, setDescription] = useState("");
  const [dueDate, setDueDate] = useState("");
  const [dueTime, setDueTime] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const inputRef = useRef<HTMLInputElement | null>(null);

  async function submit() {
    setError(null);
    setBusy(true);
    try {
      const fileIds = await uploadAll(files, "assignment_attachment");
      await api.post("/assignments", {
        student_id: studentId,
        title,
        description: description || undefined,
        due_at: dueDate && dueTime ? new Date(`${dueDate}T${dueTime}`).toISOString() : undefined,
        file_ids: fileIds.length ? fileIds : undefined,
      });
      toast({ tone: "success", title: "Задание выдано", body: "Ученик получит уведомление" });
      onCreated();
    } catch (err) {
      setError((err as Error).message);
      toast({ tone: "danger", title: "Не удалось выдать", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  function removeFile(index: number) {
    setFiles((cur) => cur.filter((_, i) => i !== index));
  }

  return (
    <Modal
      title="Выдать домашнее задание"
      subtitle="Тема, материалы и дедлайн"
      wide
      onClose={onClose}
      onSubmit={submit}
      footer={
        <>
          <Button type="button" onClick={onClose}>Отмена</Button>
          <Button variant="primary" type="submit" icon="send" loading={busy} disabled={!studentId || !title.trim()}>
            Выдать задание
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
        <Field label="Тема задания">
          <input value={title} onChange={(e) => setTitle(e.target.value)} required placeholder="Например: Квадратные уравнения · вариант 5" />
        </Field>
        <Field label="Описание">
          <textarea value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Что нужно сделать, на что обратить внимание…" />
        </Field>
        <div className="field-row modal-field-row">
          <Field label="Дедлайн · дата">
            <input type="date" value={dueDate} onChange={(e) => setDueDate(e.target.value)} />
          </Field>
          <Field label="Время" className="time-field">
            <input type="time" value={dueTime} onChange={(e) => setDueTime(e.target.value)} />
          </Field>
        </div>
        <Field label="Материалы задания" hint="PDF, изображения и др. — увидит ученик">
          {files.length > 0 && (
            <div className="attachment-list">
              {files.map((file, index) => (
                <div className="attachment-item" key={file.name + index}>
                  <Icon name={file.type.startsWith("image/") ? "image" : file.type.includes("pdf") ? "picture_as_pdf" : "description"} />
                  <div>
                    <strong>{file.name}</strong>
                    <span>{fileSize(file)}</span>
                  </div>
                  <button type="button" className="icon-button compact" onClick={() => removeFile(index)} title="Удалить" aria-label={`Удалить файл ${file.name}`}>
                    <Icon name="close" />
                  </button>
                </div>
              ))}
            </div>
          )}
          <input
            ref={inputRef}
            type="file"
            multiple
            hidden
            onChange={(e) => setFiles((cur) => [...cur, ...(e.target.files ? Array.from(e.target.files) : [])])}
          />
          <button type="button" className="upload-drop-button" onClick={() => inputRef.current?.click()}>
            <Icon name="upload_file" />
            <span>Прикрепить файл</span>
          </button>
        </Field>
      </div>
    </Modal>
  );
}
