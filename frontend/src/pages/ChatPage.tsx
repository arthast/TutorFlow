import { Fragment, useCallback, useEffect, useMemo, useRef, useState, type FormEvent } from "react";
import {
  api,
  chat,
  reports,
  type Assignment,
  type ChatDialog,
  type ChatMessage,
  type FileMeta,
  type Lesson,
  type StudentDashboard,
  type StudentLink,
  type TeacherDashboard,
} from "../api";
import { useAuth } from "../auth";
import { useOnlineStatus, useRealtimeEvent } from "../realtime";
import { AppShell, Avatar, Counter, EmptyState, ErrorMsg, FileChips, Icon, SkeletonRows, useAsync } from "../ui";
import { initials, teacherNav } from "./teacherNav";
import { studentNav } from "./studentNav";

interface ChatContact {
  id: string;
  name: string;
}

async function uploadChatFile(file: File): Promise<string> {
  const form = new FormData();
  form.append("file", file);
  form.append("purpose", "chat_message");
  const meta = await api.upload<FileMeta>("/files", form);
  return meta.id;
}

function dayLabel(iso?: string): string {
  if (!iso) return "Ранее";
  const d = new Date(iso);
  if (isNaN(d.getTime())) return "Ранее";
  const today = new Date();
  const yesterday = new Date();
  yesterday.setDate(today.getDate() - 1);
  if (d.toDateString() === today.toDateString()) return "Сегодня";
  if (d.toDateString() === yesterday.toDateString()) return "Вчера";
  return d.toLocaleDateString("ru-RU", { day: "numeric", month: "long" });
}

function timeOnly(iso?: string): string {
  if (!iso) return "";
  const d = new Date(iso);
  return isNaN(d.getTime()) ? "" : d.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" });
}

// Группировка сообщений по дню (для дата-пилюль в треде).
function groupByDate(messages: ChatMessage[]): Array<{ key: string; label: string; items: ChatMessage[] }> {
  const groups: Array<{ key: string; label: string; items: ChatMessage[] }> = [];
  for (const m of messages) {
    const key = m.created_at ? new Date(m.created_at).toDateString() : "unknown";
    const last = groups[groups.length - 1];
    if (last && last.key === key) last.items.push(m);
    else groups.push({ key, label: dayLabel(m.created_at), items: [m] });
  }
  return groups;
}

function emptyStudentDashboard(): StudentDashboard {
  return {
    student_id: "",
    total_debt_amount: 0,
    total_overpaid_amount: 0,
    pending_receipts_count: 0,
    pending_receipts_amount: 0,
    summaries: [],
  };
}

export default function ChatPage() {
  const { role } = useAuth();
  const teacherDashboard = useAsync<TeacherDashboard>(
    () => (role === "teacher" ? reports.teacherDashboard() : Promise.resolve({
      teacher_id: "",
      students_count: 0,
      upcoming_lessons_count: 0,
      pending_submissions_count: 0,
      pending_receipts_count: 0,
      pending_receipts_amount: 0,
      total_debt_amount: 0,
      total_overpaid_amount: 0,
      students_with_debt_count: 0,
      students: [],
    })),
    [role],
  );
  const students = useAsync<StudentLink[]>(
    () => (role === "teacher" ? api.get("/students") : Promise.resolve([])),
    [role],
  );
  const studentDashboard = useAsync<StudentDashboard>(
    () => (role === "student" ? reports.studentDashboard() : Promise.resolve(emptyStudentDashboard())),
    [role],
  );
  const lessons = useAsync<Lesson[]>(
    () => (role === "student" ? api.get("/lessons") : Promise.resolve([])),
    [role],
  );
  const assignments = useAsync<Assignment[]>(
    () => (role === "student" ? api.get("/assignments") : Promise.resolve([])),
    [role],
  );

  const contacts = useMemo<ChatContact[]>(() => {
    if (role === "teacher") {
      return (students.data ?? []).map((student) => ({
        id: student.student_id,
        name: student.display_name,
      }));
    }

    const names = new Map<string, string>();
    (studentDashboard.data?.summaries ?? []).forEach((summary) => {
      if (summary.teacher_id) names.set(summary.teacher_id, summary.teacher_name || summary.teacher_id.slice(0, 8));
    });
    (lessons.data ?? []).forEach((lesson) => {
      if (!names.has(lesson.teacher_id)) names.set(lesson.teacher_id, lesson.teacher_id.slice(0, 8));
    });
    (assignments.data ?? []).forEach((assignment) => {
      if (!names.has(assignment.teacher_id)) names.set(assignment.teacher_id, assignment.teacher_id.slice(0, 8));
    });
    return [...names].map(([id, name]) => ({ id, name }));
  }, [assignments.data, lessons.data, role, studentDashboard.data?.summaries, students.data]);

  const navItems = role === "teacher"
    ? teacherNav("chat", {
      students: teacherDashboard.data?.students_count,
      lessons: teacherDashboard.data?.upcoming_lessons_count,
      assignments: teacherDashboard.data?.pending_submissions_count,
      receipts: teacherDashboard.data?.pending_receipts_count,
    })
    : studentNav("chat", {
      assignments: studentDashboard.data?.summaries.reduce((sum, item) => sum + item.activity.active_assignments_count, 0),
      lessons: studentDashboard.data?.summaries.reduce((sum, item) => sum + item.activity.upcoming_lessons_count, 0),
      receipts: studentDashboard.data?.pending_receipts_count,
    });

  return (
    <AppShell
      title="Чаты"
      subtitle={role === "teacher" ? "Переписка с учениками" : "Переписка с преподавателями"}
      navSection={role === "teacher" ? "Работа" : "Учёба"}
      accent={role === "student" ? "student" : "teacher"}
      navItems={navItems}
    >
      <ChatWorkspace contacts={contacts} />
    </AppShell>
  );
}

function ChatWorkspace({ contacts }: { contacts: ChatContact[] }) {
  const { user } = useAuth();
  const selfId = user?.user_id ?? "";
  const [dialogs, setDialogs] = useState<ChatDialog[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [text, setText] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [newContact, setNewContact] = useState("");
  const [query, setQuery] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [loadingMessages, setLoadingMessages] = useState(false);
  const [loadingDialogs, setLoadingDialogs] = useState(true);
  // Live «✓✓ прочитано»: id последнего сообщения, прочитанного собеседником
  // (по realtime chat.read; после reload теряется — персистентного маркера в API нет).
  const [peerReadUpToId, setPeerReadUpToId] = useState<string | null>(null);
  const markedRef = useRef("");
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const threadRef = useRef<HTMLDivElement | null>(null);
  const forceScrollRef = useRef(true);

  function scrollToBottom() {
    window.requestAnimationFrame(() => {
      const el = threadRef.current;
      if (el) el.scrollTop = el.scrollHeight;
    });
  }

  const nameById = useCallback(
    (id: string) => contacts.find((contact) => contact.id === id)?.name ?? id.slice(0, 8),
    [contacts],
  );

  const otherId = useCallback(
    (dialog: ChatDialog) => (dialog.teacher_id === selfId ? dialog.student_id : dialog.teacher_id),
    [selfId],
  );

  const selectedDialog = dialogs.find((dialog) => dialog.id === selectedId) ?? null;
  const selectedPeerId = selectedDialog ? otherId(selectedDialog) : "";
  const selectedPeerName = selectedPeerId ? nameById(selectedPeerId) : "";

  const loadDialogs = useCallback(async () => {
    try {
      const items = await chat.listDialogs();
      setDialogs(items);
      setSelectedId((current) => current ?? items[0]?.id ?? null);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setLoadingDialogs(false);
    }
  }, []);

  const loadMessages = useCallback(
    async (dialogId: string) => {
      const el = threadRef.current;
      const wasNearBottom = !el || el.scrollHeight - el.scrollTop - el.clientHeight < 140;
      try {
        const items = await chat.listMessages(dialogId);
        setMessages(items);
        const last = items[items.length - 1];
        if (last && last.sender_id !== selfId && last.id !== markedRef.current) {
          markedRef.current = last.id;
          try {
            await chat.markRead(dialogId, last.id);
            loadDialogs();
          } catch {
            /* read marker is not required for rendering */
          }
        }
        // Автоскролл: при открытии диалога всегда, при обновлениях — только если пользователь внизу.
        if (forceScrollRef.current || wasNearBottom) {
          forceScrollRef.current = false;
          scrollToBottom();
        }
      } catch (err) {
        setError((err as Error).message);
      } finally {
        setLoadingMessages(false);
      }
    },
    [loadDialogs, selfId],
  );

  useEffect(() => {
    loadDialogs();
    const timer = window.setInterval(loadDialogs, 5000);
    return () => window.clearInterval(timer);
  }, [loadDialogs]);

  useEffect(() => {
    if (!selectedId) return;
    markedRef.current = "";
    forceScrollRef.current = true;
    setPeerReadUpToId(null);
    setLoadingMessages(true);
    loadMessages(selectedId);
    const timer = window.setInterval(() => loadMessages(selectedId), 3000);
    return () => window.clearInterval(timer);
  }, [loadMessages, selectedId]);

  useRealtimeEvent((event) => {
    if (event.type !== "chat.message" && event.type !== "chat.read") return;
    loadDialogs();
    const dialogId = String(event.payload.dialog_id ?? "");
    if (!selectedId || dialogId !== selectedId) return;
    if (event.type === "chat.read") {
      // Маркер собеседника (свой read-маркер игнорируем) → «✓✓» на своих сообщениях.
      // realtime-service доставляет его peer'у с payload {dialog_id, reader_id, up_to_message_id}.
      const readerId = String(event.payload.reader_id ?? event.payload.user_id ?? "");
      const upTo = String(event.payload.up_to_message_id ?? event.payload.last_read_message_id ?? "");
      if (readerId !== selfId && upTo) setPeerReadUpToId(upTo);
      return;
    }
    loadMessages(selectedId);
  }, [selectedId, selfId, loadDialogs, loadMessages]);

  // Индекс последнего прочитанного собеседником сообщения в открытом треде.
  const peerReadIndex = useMemo(
    () => (peerReadUpToId ? messages.findIndex((m) => m.id === peerReadUpToId) : -1),
    [messages, peerReadUpToId],
  );

  const visibleDialogs = useMemo(() => {
    const q = query.trim().toLowerCase();
    return dialogs.filter((dialog) => {
      if (!q) return true;
      const peerId = otherId(dialog);
      return nameById(peerId).toLowerCase().includes(q) || (dialog.last_message?.text ?? "").toLowerCase().includes(q);
    });
  }, [dialogs, nameById, otherId, query]);

  async function startDialog(event: FormEvent) {
    event.preventDefault();
    if (!newContact) return;
    setBusy(true);
    setError(null);
    try {
      const dialog = await chat.createDialog(newContact);
      setNewContact("");
      await loadDialogs();
      setSelectedId(dialog.id);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  async function send(event: FormEvent) {
    event.preventDefault();
    if (!selectedId) return;
    if (!text.trim() && !file) {
      setError("Введите сообщение или прикрепите файл");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      const fileIds = file ? [await uploadChatFile(file)] : undefined;
      await chat.sendMessage(selectedId, text.trim(), fileIds);
      setText("");
      setFile(null);
      if (fileInputRef.current) fileInputRef.current.value = "";
      forceScrollRef.current = true;
      await loadMessages(selectedId);
      loadDialogs();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="chat-page">
      <aside className="chat-page-list">
        <form onSubmit={startDialog} className="chat-start-form">
          <select value={newContact} onChange={(event) => setNewContact(event.target.value)} aria-label="Контакт для нового чата">
            <option value="">— начать чат —</option>
            {contacts.map((contact) => (
              <option key={contact.id} value={contact.id}>{contact.name}</option>
            ))}
          </select>
          <button className="primary small" type="submit" disabled={busy || !newContact}>Открыть</button>
        </form>
        <div className="chat-page-search">
          <Icon name="search" />
          <input placeholder="Поиск по диалогам…" aria-label="Поиск по диалогам" value={query} onChange={(event) => setQuery(event.target.value)} />
        </div>
        <ErrorMsg error={error} />
        <div className="chat-page-dialogs tf-scroll">
          {loadingDialogs && dialogs.length === 0 ? (
            <SkeletonRows count={4} />
          ) : visibleDialogs.length === 0 ? (
            <EmptyState
              icon="forum"
              title={query ? "Ничего не найдено" : "Диалогов пока нет"}
              hint={query ? "Измените поисковый запрос." : "Выберите контакт выше, чтобы начать чат."}
            />
          ) : (
            visibleDialogs.map((dialog) => (
              <FullDialogItem
                key={dialog.id}
                dialog={dialog}
                active={dialog.id === selectedId}
                peerId={otherId(dialog)}
                peerName={nameById(otherId(dialog))}
                onOpen={() => {
                  setSelectedId(dialog.id);
                  setMessages([]);
                }}
              />
            ))
          )}
        </div>
      </aside>

      <section className="chat-page-thread">
        {selectedDialog ? (
          <>
            <ThreadHeader peerId={selectedPeerId} peerName={selectedPeerName} />
            <div className="chat-page-messages tf-scroll" ref={threadRef}>
              {loadingMessages && messages.length === 0 ? (
                <div style={{ width: "100%" }}><SkeletonRows count={3} /></div>
              ) : messages.length === 0 ? (
                <div className="chat-empty">
                  <Icon name="forum" />
                  <strong>Пока нет сообщений</strong>
                  <span>Напишите первое сообщение ниже.</span>
                </div>
              ) : (
                groupByDate(messages).map((group) => (
                  <Fragment key={group.key}>
                    <div className="chat-date-pill">{group.label}</div>
                    {group.items.map((message) => (
                      <MessageBubble
                        key={message.id}
                        message={message}
                        mine={message.sender_id === selfId}
                        read={peerReadIndex >= 0 && messages.indexOf(message) <= peerReadIndex}
                      />
                    ))}
                  </Fragment>
                ))
              )}
            </div>
            <form className="chat-page-compose" onSubmit={send}>
              <button
                className="icon-button"
                type="button"
                onClick={() => fileInputRef.current?.click()}
                title="Прикрепить файл"
                aria-label="Прикрепить файл"
              >
                <Icon name="attach_file" />
              </button>
              <input ref={fileInputRef} type="file" onChange={(event) => setFile(event.target.files?.[0] ?? null)} hidden />
              <textarea
                value={text}
                onChange={(event) => setText(event.target.value)}
                onKeyDown={(event) => {
                  if (event.key === "Enter" && !event.shiftKey) {
                    event.preventDefault();
                    event.currentTarget.form?.requestSubmit();
                  }
                }}
                placeholder={file ? `Файл: ${file.name}` : "Написать сообщение…"}
                aria-label="Текст сообщения"
              />
              <button className="primary icon-button" type="submit" disabled={busy} title="Отправить" aria-label="Отправить сообщение">
                <Icon name="send" />
              </button>
            </form>
          </>
        ) : (
          <div className="chat-empty">
            <Icon name="chat_bubble" />
            <strong>Выберите диалог</strong>
            <span>Или начните новый чат из списка контактов.</span>
          </div>
        )}
      </section>
    </div>
  );
}

function ThreadHeader({ peerId, peerName }: { peerId: string; peerName: string }) {
  const online = useOnlineStatus(peerId);
  return (
    <header className="chat-thread-header">
      <Avatar name={peerName} presence={online ? "online" : "away"} />
      <div>
        <div className="summary-title">{peerName}</div>
        <div className={online ? "presence online" : "presence"}><span />{online ? "в сети" : "не в сети"}</div>
      </div>
      <div className="chat-thread-actions">
        <button className="icon-button" type="button" title="Занятия" aria-label="Перейти к занятиям"><Icon name="calendar_add_on" /></button>
        <button className="icon-button" type="button" title="Ещё" aria-label="Дополнительные действия"><Icon name="more_horiz" /></button>
      </div>
    </header>
  );
}

function FullDialogItem({
  dialog,
  active,
  peerId,
  peerName,
  onOpen,
}: {
  dialog: ChatDialog;
  active: boolean;
  peerId: string;
  peerName: string;
  onOpen: () => void;
}) {
  const online = useOnlineStatus(peerId);
  return (
    <button className={"chat-page-dialog" + (active ? " active" : "")} onClick={onOpen}>
      <span className="avatar dialog-avatar">{initials(peerName)}</span>
      <span className={online ? "presence-dot online" : "presence-dot"} />
      <span className="dialog-main">
        <span className="dialog-title-row">
          <strong>{peerName}</strong>
          <span>{dialogTime(dialog)}</span>
        </span>
        <span className={dialog.unread_count > 0 ? "dialog-preview unread" : "dialog-preview"}>
          {dialog.last_message?.text || (dialog.last_message ? "вложение" : "Нет сообщений")}
        </span>
      </span>
      <Counter value={dialog.unread_count} tone="accent" />
    </button>
  );
}

// Короткое время для строки диалога: сегодня → ЧЧ:ММ, иначе день/месяц.
function dialogTime(dialog: ChatDialog): string {
  const iso = dialog.last_message_at ?? dialog.created_at;
  if (!iso) return "";
  const d = new Date(iso);
  if (isNaN(d.getTime())) return "";
  if (d.toDateString() === new Date().toDateString()) return timeOnly(iso);
  return d.toLocaleDateString("ru-RU", { day: "numeric", month: "short" });
}

function MessageBubble({ message, mine, read }: { message: ChatMessage; mine: boolean; read: boolean }) {
  return (
    <div className={"chat-page-message" + (mine ? " mine" : "")}>
      <div className="chat-page-bubble">
        {message.text && <div className="chat-msg-text">{message.text}</div>}
        <FileChips fileIds={message.file_ids} />
      </div>
      <div className="message-meta">
        <span>{timeOnly(message.created_at)}</span>
        {mine && (
          <Icon
            name={read ? "done_all" : "done"}
            className={read ? "msg-read" : "msg-sent"}
          />
        )}
        {mine && read && <span className="visually-hidden">прочитано</span>}
      </div>
    </div>
  );
}
