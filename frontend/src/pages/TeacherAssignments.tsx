import { useMemo, useState, type FormEvent } from "react";
import { Link } from "react-router-dom";
import {
  api,
  reports,
  type Assignment,
  type FileMeta,
  type StudentLink,
  type TeacherDashboard,
} from "../api";
import { AppShell, Card, ErrorMsg, Icon, ListState, StatusPill, useAsync } from "../ui";
import { teacherNav } from "./teacherNav";

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

function studentName(students: StudentLink[], studentId: string): string {
  return students.find((student) => student.student_id === studentId)?.display_name ?? studentId.slice(0, 8);
}

export default function TeacherAssignments() {
  const dashboard = useAsync<TeacherDashboard>(() => reports.teacherDashboard(), []);
  const students = useAsync<StudentLink[]>(() => api.get("/students"), []);
  const assignments = useAsync<Assignment[]>(() => api.get("/assignments"), []);
  const [status, setStatus] = useState("all");
  const [query, setQuery] = useState("");
  const [createOpen, setCreateOpen] = useState(false);

  const studentList = students.data ?? [];
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return (assignments.data ?? []).filter((assignment) => {
      const name = studentName(studentList, assignment.student_id).toLowerCase();
      const matchesQuery = !q || assignment.title.toLowerCase().includes(q) || name.includes(q);
      const matchesStatus = status === "all" || assignment.status === status;
      return matchesQuery && matchesStatus;
    });
  }, [assignments.data, query, status, studentList]);

  return (
    <AppShell
      title="Домашние задания"
      subtitle="Выдача, сдача и проверка работ"
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
          <span>Новое ДЗ</span>
        </button>
      }
    >
      <div className="container">
        <div className="teacher-toolbar">
          <div className="segmented">
            {[
              ["all", "Все"],
              ["assigned", "Выдано"],
              ["submitted", "Сдано"],
              ["needs_fix", "На правках"],
              ["reviewed", "Проверено"],
            ].map(([value, label]) => (
              <button className={status === value ? "active" : ""} key={value} onClick={() => setStatus(value)}>
                {label}
              </button>
            ))}
          </div>
          <div className="search-field">
            <Icon name="search" />
            <input placeholder="Поиск по заданию или ученику..." value={query} onChange={(event) => setQuery(event.target.value)} />
          </div>
        </div>

        <Card title="Все задания" icon="assignment">
          {filtered.map((assignment) => (
            <div className="resource-row" key={assignment.id}>
              <div className="resource-icon"><Icon name="assignment" /></div>
              <div className="resource-main">
                <Link className="summary-title" to={`/teacher/assignments/${assignment.id}/review`}>{assignment.title}</Link>
                <div className="summary-grid">
                  <span>{studentName(studentList, assignment.student_id)}</span>
                  <span>Создано: {assignment.created_at ? new Date(assignment.created_at).toLocaleDateString("ru-RU") : "-"}</span>
                  <span>{assignment.description || "Описание не указано"}</span>
                </div>
              </div>
              <div className="btn-group">
                <Link className="button-link small-link" to={`/teacher/assignments/${assignment.id}/review`}>Проверка</Link>
                <StatusPill status={assignment.status} />
              </div>
            </div>
          ))}
          <ListState query={{ ...assignments, data: filtered }} empty="Задания не найдены." />
        </Card>
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
  const [files, setFiles] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

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
        file_ids: fileIds.length ? fileIds : undefined,
      });
      onCreated();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="modal-overlay" onMouseDown={onClose}>
      <form className="modal-panel" onMouseDown={(event) => event.stopPropagation()} onSubmit={create}>
        <div className="modal-heading">
          <div>
            <h2>Новое ДЗ</h2>
            <p>Черновой skeleton до отдельного дизайна</p>
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
          <div className="field"><label>Название<input value={title} onChange={(event) => setTitle(event.target.value)} required /></label></div>
          <div className="field"><label>Описание<textarea value={description} onChange={(event) => setDescription(event.target.value)} /></label></div>
          <div className="field"><label>Файлы<input type="file" multiple onChange={(event) => setFiles(event.target.files ? Array.from(event.target.files) : [])} /></label></div>
        </div>
        <div className="modal-actions">
          <button type="button" onClick={onClose}>Отмена</button>
          <button className="primary" type="submit" disabled={busy}>
            <Icon name="assignment_add" />
            {busy ? "Создание..." : "Создать ДЗ"}
          </button>
        </div>
      </form>
    </div>
  );
}
