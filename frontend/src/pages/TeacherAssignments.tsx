import { useMemo, useRef, useState, type FormEvent } from "react";
import { Link } from "react-router-dom";
import {
  api,
  reports,
  type Assignment,
  type FileMeta,
  type StudentLink,
  type TeacherDashboard,
} from "../api";
import { AppShell, ErrorMsg, Icon, ListState, StatusPill, useAsync } from "../ui";
import { teacherNav } from "./teacherNav";

type AssignmentTab = "all" | "review" | "active" | "done";

const ASSIGNMENT_STATUS: Record<string, {
  label: string;
  category: Exclude<AssignmentTab, "all">;
  accent: string;
  icon: string;
  iconClass: string;
}> = {
  assigned: { label: "выдано", category: "active", accent: "#3b5bdb", icon: "assignment", iconClass: "assignment-icon-info" },
  submitted: { label: "сдано", category: "review", accent: "#d99413", icon: "assignment_turned_in", iconClass: "assignment-icon-warning" },
  needs_fix: { label: "нужны правки", category: "active", accent: "#d99413", icon: "edit_note", iconClass: "assignment-icon-warning" },
  reviewed: { label: "выполнено", category: "done", accent: "#18a866", icon: "task_alt", iconClass: "assignment-icon-success" },
  accepted: { label: "зачтено", category: "done", accent: "#18a866", icon: "task_alt", iconClass: "assignment-icon-success" },
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
  return students.find((student) => student.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}

function initials(name: string): string {
  return name.split(/\s+/).filter(Boolean).slice(0, 2).map((part) => part[0]?.toUpperCase()).join("") || "??";
}

function deadlineLabel(assignment: Assignment): string {
  if (assignment.due_at) {
    const date = new Date(assignment.due_at);
    if (!isNaN(date.getTime())) {
      return date.toLocaleString("ru-RU", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
    }
  }
  return assignment.created_at ? `создано ${new Date(assignment.created_at).toLocaleDateString("ru-RU")}` : "без дедлайна";
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
  const counts = useMemo(() => {
    const base: Record<AssignmentTab, number> = { all: 0, review: 0, active: 0, done: 0 };
    (assignments.data ?? []).forEach((assignment) => {
      const category = statusMeta(assignment.status).category;
      base.all += 1;
      base[category] += 1;
    });
    return base;
  }, [assignments.data]);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return (assignments.data ?? []).filter((assignment) => {
      const name = studentName(studentList, assignment.student_id).toLowerCase();
      const matchesQuery = !q || assignment.title.toLowerCase().includes(q) || name.includes(q);
      const matchesTab = tab === "all" || statusMeta(assignment.status).category === tab;
      return matchesQuery && matchesTab;
    });
  }, [assignments.data, query, studentList, tab]);

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
      actions={
        <button className="primary-action" type="button" onClick={() => setCreateOpen(true)}>
          <Icon name="add" />
          <span>Выдать ДЗ</span>
        </button>
      }
    >
      <div className="container">
        <div className="page-tabs-bar">
          <div className="page-tabs">
            {[
              ["all", "Все"],
              ["review", "На проверку"],
              ["active", "В работе"],
              ["done", "Выполнено"],
            ].map(([value, label]) => (
              <button className={tab === value ? "active" : ""} key={value} onClick={() => setTab(value as AssignmentTab)}>
                {label}
                <span>{counts[value as AssignmentTab]}</span>
              </button>
            ))}
          </div>
          <div className="search-field">
            <Icon name="search" />
            <input placeholder="Поиск по теме или ученику..." value={query} onChange={(event) => setQuery(event.target.value)} />
          </div>
        </div>

        <div className="assignment-list">
          {filtered.map((assignment) => (
            <AssignmentRow assignment={assignment} students={studentList} key={assignment.id} />
          ))}
          <ListState query={{ ...assignments, data: filtered }} empty="В этой вкладке пока пусто." />
        </div>
      </div>

      {createOpen && (
        <CreateAssignmentModal
          students={students.data ?? []}
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
          <span>
            <span className="mini-avatar">{initials(name)}</span>
            {name}
          </span>
          <span className={assignment.due_at ? "deadline" : ""}><Icon name="schedule" />{deadlineLabel(assignment)}</span>
        </div>
      </div>
      <StatusPill status={assignment.status} />
      <Link className={review ? "button-link small-link primary-link" : "button-link small-link"} to={`/teacher/assignments/${assignment.id}/review`}>
        {review ? "Проверить" : "Подробнее"}
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
  const [studentId, setStudentId] = useState("");
  const [title, setTitle] = useState("");
  const [description, setDescription] = useState("");
  const [dueDate, setDueDate] = useState("");
  const [dueTime, setDueTime] = useState("");
  const [files, setFiles] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const inputRef = useRef<HTMLInputElement | null>(null);

  async function create(event: FormEvent) {
    event.preventDefault();
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
      onCreated();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  function removeFile(index: number) {
    setFiles((current) => current.filter((_, itemIndex) => itemIndex !== index));
  }

  return (
    <div className="modal-overlay" onMouseDown={onClose}>
      <form className="modal-panel modal-panel-wide" onMouseDown={(event) => event.stopPropagation()} onSubmit={create}>
        <div className="modal-heading">
          <div>
            <h2>Выдать домашнее задание</h2>
            <p>Тема, материалы и дедлайн</p>
          </div>
          <button className="icon-button" type="button" onClick={onClose} title="Закрыть"><Icon name="close" /></button>
        </div>
        <ErrorMsg error={error} />
        <div className="modal-fields">
          <div className="field">
            <label>Ученик
              <span className="select-wrap">
                <select value={studentId} onChange={(event) => setStudentId(event.target.value)} required>
                  <option value="">— выбрать —</option>
                  {students.map((student) => (
                    <option key={student.id} value={student.student_id}>{student.display_name}</option>
                  ))}
                </select>
                <Icon name="expand_more" />
              </span>
            </label>
          </div>
          <div className="field"><label>Тема задания<input value={title} onChange={(event) => setTitle(event.target.value)} required placeholder="Например: Квадратные уравнения · вариант 5" /></label></div>
          <div className="field"><label>Описание<textarea value={description} onChange={(event) => setDescription(event.target.value)} placeholder="Что нужно сделать, на что обратить внимание..." /></label></div>
          <div className="field-row modal-field-row">
            <label>Дедлайн · дата<input type="date" value={dueDate} onChange={(event) => setDueDate(event.target.value)} /></label>
            <label className="time-field">Время<input type="time" value={dueTime} onChange={(event) => setDueTime(event.target.value)} /></label>
          </div>
          <div className="field">
            <label>Материалы задания</label>
            {files.length > 0 && (
              <div className="attachment-list">
                {files.map((file, index) => (
                  <div className="attachment-item" key={file.name + index}>
                    <Icon name={file.type.startsWith("image/") ? "image" : file.type.includes("pdf") ? "picture_as_pdf" : "description"} />
                    <div>
                      <strong>{file.name}</strong>
                      <span>{fileSize(file)}</span>
                    </div>
                    <button type="button" className="icon-button compact" onClick={() => removeFile(index)} title="Удалить">
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
              onChange={(event) => setFiles(event.target.files ? Array.from(event.target.files) : [])}
            />
            <button type="button" className="upload-drop-button" onClick={() => inputRef.current?.click()}>
              <Icon name="upload_file" />
              <span>Прикрепить файл</span>
            </button>
          </div>
        </div>
        <div className="modal-actions">
          <button type="button" onClick={onClose}>Отмена</button>
          <button className="primary" type="submit" disabled={busy}>
            <Icon name="send" />
            {busy ? "Выдача..." : "Выдать задание"}
          </button>
        </div>
      </form>
    </div>
  );
}
