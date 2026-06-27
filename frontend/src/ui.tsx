import { useCallback, useEffect, useState, type ReactNode } from "react";
import { api, openFile, type AppNotification, type FileMeta } from "./api";
import { useAuth } from "./auth";
import { useRealtimeEvent } from "./realtime";

export function TopBar() {
  const { user, role, logout } = useAuth();
  return (
    <div className="topbar">
      <div className="brand">TutorFlow</div>
      <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
        <span className={"role-chip " + (role === "teacher" ? "role-teacher" : "role-student")}>
          {role === "teacher" ? "Преподаватель" : "Ученик"}
        </span>
        <span className="muted">{user?.display_name}</span>
        <button className="small" onClick={logout}>Выйти</button>
      </div>
    </div>
  );
}

export function Card({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="card">
      <h3>{title}</h3>
      {children}
    </div>
  );
}

const PILL_CLASS: Record<string, string> = {
  scheduled: "pill-info",
  completed: "pill-success",
  cancelled: "pill-muted",
  active: "pill-success",
  invited: "pill-muted",
  archived: "pill-muted",
  assigned: "pill-info",
  submitted: "pill-warning",
  reviewed: "pill-success",
  needs_fix: "pill-warning",
  accepted: "pill-success",
  done: "pill-success",
  pending_review: "pill-warning",
  confirmed: "pill-success",
  rejected: "pill-danger",
};

export function StatusPill({ status }: { status: string }) {
  return <span className={"pill " + (PILL_CLASS[status] ?? "pill-muted")}>{status}</span>;
}

export function ErrorMsg({ error }: { error: string | null }) {
  if (!error) return null;
  return <div className="error">{error}</div>;
}

// Зелёное уведомление об успехе действия.
export function Notice({ text }: { text: string | null }) {
  if (!text) return null;
  return <div className="notice">{text}</div>;
}

// Единообразные состояния списка: загрузка / ошибка (+повтор) / пусто.
// Возвращает узел-состояние или null, если данные готовы и не пусты.
export function ListState<T>({
  query,
  empty = "Пока ничего нет.",
}: {
  query: { data: T[] | null; error: string | null; loading: boolean; reload: () => void };
  empty?: string;
}) {
  if (query.loading && !query.data) return <p className="hint">Загрузка…</p>;
  if (query.error) {
    return (
      <div>
        <ErrorMsg error={query.error} />
        <button className="small" onClick={query.reload}>Повторить</button>
      </div>
    );
  }
  if ((query.data ?? []).length === 0) return <p className="hint">{empty}</p>;
  return null;
}

export function NotificationsCard() {
  const notifications = useAsync<AppNotification[]>(() => api.get("/notifications"), []);
  const [error, setError] = useState<string | null>(null);
  const [actingId, setActingId] = useState<string | null>(null);
  const items = notifications.data ?? [];
  const unread = items.filter((n) => !n.is_read).length;

  useRealtimeEvent((event) => {
    if (event.type === "notification") notifications.reload();
  }, [notifications.reload]);

  async function markRead(id: string) {
    setError(null);
    setActingId(id);
    try {
      await api.post(`/notifications/${id}/read`);
      notifications.reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setActingId(null);
    }
  }

  return (
    <Card title={unread ? `Уведомления (${unread})` : "Уведомления"}>
      <ErrorMsg error={error} />
      {items.map((n) => (
        <div className={"notification " + (n.is_read ? "notification-read" : "notification-unread")} key={n.id}>
          <div>
            <div className="notification-title">{n.title}</div>
            <div className="muted">{n.body}</div>
            <div className="hint">{fmtDate(n.created_at)}</div>
          </div>
          {!n.is_read && (
            <button className="small" disabled={actingId === n.id} onClick={() => markRead(n.id)}>
              {actingId === n.id ? "…" : "Прочитано"}
            </button>
          )}
        </div>
      ))}
      <ListState query={notifications} empty="Новых уведомлений нет." />
    </Card>
  );
}

// Список файлов по file_ids: имя (если доступно) + скачать/открыть.
export function FileChips({ fileIds, label }: { fileIds?: string[]; label?: string }) {
  if (!fileIds || fileIds.length === 0) return null;
  return (
    <div style={{ margin: "6px 0" }}>
      {label && <p className="section-title">{label}</p>}
      <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
        {fileIds.map((id) => <FileChip key={id} id={id} />)}
      </div>
    </div>
  );
}

function FileChip({ id }: { id: string }) {
  const meta = useAsync<FileMeta>(() => api.get(`/files/${id}`), [id]);
  const [err, setErr] = useState<string | null>(null);
  const name = meta.data?.original_name || "файл";
  return (
    <button className="small" title={err ?? "Скачать"} onClick={() => openFile(id).catch((e) => setErr((e as Error).message))}>
      {name}
    </button>
  );
}

// Загрузка данных с состоянием + ручной reload.
export function useAsync<T>(fn: () => Promise<T>, deps: unknown[] = []) {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  const run = useCallback(() => {
    setLoading(true);
    setError(null);
    fn()
      .then(setData)
      .catch((e: Error) => setError(e.message))
      .finally(() => setLoading(false));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);

  useEffect(run, [run]);
  return { data, error, loading, reload: run };
}

export function fmtDate(iso?: string): string {
  if (!iso) return "";
  const d = new Date(iso);
  if (isNaN(d.getTime())) return iso;
  return d.toLocaleString("ru-RU", { dateStyle: "medium", timeStyle: "short" });
}
