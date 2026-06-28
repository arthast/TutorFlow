import { useCallback, useEffect, useRef, useState, type FormEvent } from "react";
import { api, chat, type ChatDialog, type ChatMessage, type FileMeta } from "./api";
import { useAuth } from "./auth";
import { useOnlineStatus, useRealtimeEvent } from "./realtime";
import { Card, ErrorMsg, FileChips, fmtDate } from "./ui";

export interface ChatContact {
  id: string;
  name: string;
}

async function uploadOne(file: File): Promise<string> {
  const form = new FormData();
  form.append("file", file);
  form.append("purpose", "chat_message");
  const meta = await api.upload<FileMeta>("/files", form);
  return meta.id;
}

// Личная переписка teacher<->student. Отправка остаётся REST; realtime только
// ускоряет обновление списка/открытого окна поверх polling fallback.
export function ChatCard({ contacts, id = "chat" }: { contacts: ChatContact[]; id?: string }) {
  const { user } = useAuth();
  const selfId = user?.user_id ?? "";

  const [dialogs, setDialogs] = useState<ChatDialog[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [text, setText] = useState("");
  const [file, setFile] = useState<File | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [newContact, setNewContact] = useState("");
  const markedRef = useRef<string>("");

  const nameById = useCallback(
    (id: string) => contacts.find((c) => c.id === id)?.name ?? id.slice(0, 8),
    [contacts],
  );
  const otherId = useCallback(
    (d: ChatDialog) => (d.teacher_id === selfId ? d.student_id : d.teacher_id),
    [selfId],
  );

  const loadDialogs = useCallback(async () => {
    try {
      setDialogs(await chat.listDialogs());
    } catch (e) {
      setError((e as Error).message);
    }
  }, []);

  useEffect(() => {
    loadDialogs();
    const t = setInterval(loadDialogs, 5000);
    return () => clearInterval(t);
  }, [loadDialogs]);

  const loadMessages = useCallback(
    async (dialogId: string) => {
      try {
        const msgs = await chat.listMessages(dialogId);
        setMessages(msgs);
        const last = msgs[msgs.length - 1];
        if (last && last.id !== markedRef.current) {
          markedRef.current = last.id;
          try {
            await chat.markRead(dialogId, last.id);
            loadDialogs();
          } catch {
            /* mark-read не критичен для отображения */
          }
        }
      } catch (e) {
        setError((e as Error).message);
      }
    },
    [loadDialogs],
  );

  useEffect(() => {
    if (!selectedId) return;
    markedRef.current = "";
    loadMessages(selectedId);
    const t = setInterval(() => loadMessages(selectedId), 3000);
    return () => clearInterval(t);
  }, [selectedId, loadMessages]);

  useRealtimeEvent((event) => {
    if (event.type !== "chat.message" && event.type !== "chat.read") return;
    loadDialogs();
    const dialogId = String(event.payload.dialog_id ?? "");
    if (selectedId && dialogId === selectedId) {
      loadMessages(selectedId);
    }
  }, [selectedId, loadDialogs, loadMessages]);

  function openDialog(id: string) {
    setSelectedId(id);
    setMessages([]);
    setError(null);
  }

  async function startDialog(e: FormEvent) {
    e.preventDefault();
    if (!newContact) return;
    setError(null);
    setBusy(true);
    try {
      const d = await chat.createDialog(newContact);
      setNewContact("");
      await loadDialogs();
      openDialog(d.id);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  }

  async function send(e: FormEvent) {
    e.preventDefault();
    if (!selectedId) return;
    if (!text.trim() && !file) {
      setError("Введите сообщение или прикрепите файл");
      return;
    }
    setError(null);
    setBusy(true);
    try {
      let fileIds: string[] | undefined;
      if (file) fileIds = [await uploadOne(file)];
      await chat.sendMessage(selectedId, text.trim(), fileIds);
      setText("");
      setFile(null);
      await loadMessages(selectedId);
      loadDialogs();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Сообщения" icon="chat_bubble" id={id}>
      <ErrorMsg error={error} />
      <div className="chat-layout">
        <div className="chat-dialogs">
          <form onSubmit={startDialog} className="field field-row" style={{ marginBottom: 8 }}>
            <select value={newContact} onChange={(e) => setNewContact(e.target.value)} style={{ flex: 1 }}>
              <option value="">— начать чат —</option>
              {contacts.map((c) => (
                <option key={c.id} value={c.id}>{c.name}</option>
              ))}
            </select>
            <button className="small" type="submit" disabled={busy || !newContact}>Открыть</button>
          </form>
          {dialogs.map((d) => (
            <DialogItem
              key={d.id}
              dialog={d}
              active={selectedId === d.id}
              peerId={otherId(d)}
              peerName={nameById(otherId(d))}
              onOpen={() => openDialog(d.id)}
            />
          ))}
          {dialogs.length === 0 && <p className="hint">Диалогов пока нет.</p>}
        </div>
        <div className="chat-window">
          {!selectedId && <p className="hint">Выберите диалог или начните новый.</p>}
          {selectedId && (
            <>
              <div className="chat-messages">
                {messages.map((m) => (
                  <div key={m.id} className={"chat-msg" + (m.sender_id === selfId ? " mine" : "")}>
                    {m.text && <div className="chat-msg-text">{m.text}</div>}
                    <FileChips fileIds={m.file_ids} />
                    <div className="hint">{fmtDate(m.created_at)}</div>
                  </div>
                ))}
                {messages.length === 0 && <p className="hint">Сообщений пока нет.</p>}
              </div>
              <form onSubmit={send} className="chat-compose">
                <input placeholder="Сообщение…" value={text} onChange={(e) => setText(e.target.value)} />
                <input type="file" onChange={(e) => setFile(e.target.files?.[0] ?? null)} />
                <button className="primary small" type="submit" disabled={busy}>{busy ? "…" : "Отправить"}</button>
              </form>
            </>
          )}
        </div>
      </div>
    </Card>
  );
}

function DialogItem({
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
    <button
      className={"chat-dialog-item" + (active ? " active" : "")}
      onClick={onOpen}
    >
      <span className={"chat-dialog-name" + (online ? " online" : "")}>{peerName}</span>
      {dialog.unread_count > 0 && <span className="chat-unread">{dialog.unread_count}</span>}
      <span className="hint chat-dialog-preview">
        {dialog.last_message?.text || (dialog.last_message ? "вложение" : "")}
      </span>
    </button>
  );
}
