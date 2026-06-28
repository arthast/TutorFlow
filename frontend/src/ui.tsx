import { useCallback, useEffect, useState, type ReactNode } from "react";
import { api, openFile, type AppNotification, type FileMeta } from "./api";
import { useAuth } from "./auth";
import { useRealtimeEvent } from "./realtime";

export function Icon({ name, className = "" }: { name: string; className?: string }) {
  return <span className={"ms " + className} aria-hidden="true">{name}</span>;
}

export interface AppNavItem {
  label: string;
  icon: string;
  href: string;
  badge?: number | string;
  active?: boolean;
}

function initials(name?: string): string {
  const parts = (name ?? "TutorFlow").trim().split(/\s+/).filter(Boolean);
  if (parts.length === 0) return "TF";
  return parts.slice(0, 2).map((p) => p[0]?.toUpperCase()).join("");
}

export function AppShell({
  title,
  subtitle,
  navSection,
  navItems,
  accent = "teacher",
  actions,
  children,
}: {
  title: string;
  subtitle?: string;
  navSection: string;
  navItems: AppNavItem[];
  accent?: "teacher" | "student";
  actions?: ReactNode;
  children: ReactNode;
}) {
  const { user, role, logout } = useAuth();
  const [collapsed, setCollapsed] = useState(() => {
    try {
      const saved = localStorage.getItem("tf_sb_collapsed");
      if (saved !== null) return saved === "1";
    } catch {
      /* localStorage may be unavailable in private contexts */
    }
    return typeof window !== "undefined" && window.innerWidth < 1080;
  });

  useEffect(() => {
    function onResize() {
      if (window.innerWidth < 880) setCollapsed(true);
    }
    window.addEventListener("resize", onResize);
    return () => window.removeEventListener("resize", onResize);
  }, []);

  function toggle() {
    setCollapsed((next) => {
      const value = !next;
      try {
        localStorage.setItem("tf_sb_collapsed", value ? "1" : "0");
      } catch {
        /* noop */
      }
      return value;
    });
  }

  const roleLabel = role === "teacher" ? "Преподаватель" : "Ученик";

  return (
    <div className={"app-shell app-shell-" + accent + (collapsed ? " is-collapsed" : "")}>
      <aside className="sidebar">
        <div className="sidebar-brand">
          <div className="brand-mark">T</div>
          {!collapsed && <span>TutorFlow</span>}
        </div>
        <nav className="sidebar-nav tf-scroll">
          {!collapsed && <div className="sidebar-section">{navSection}</div>}
          {navItems.map((item, index) => {
            const routeActive =
              typeof window !== "undefined" &&
              item.href.startsWith("/") &&
              window.location.pathname === item.href;
            const isActive = item.active ?? (routeActive || index === 0);
            return (
              <a className={"sidebar-link" + (isActive ? " active" : "")} href={item.href} key={item.href} title={item.label}>
                <Icon name={item.icon} />
                {!collapsed && <span>{item.label}</span>}
                {item.badge !== undefined && item.badge !== 0 && (
                  <span className="nav-badge">{item.badge}</span>
                )}
              </a>
            );
          })}
        </nav>
        <div className="sidebar-user">
          <div className="avatar">{initials(user?.display_name)}</div>
          {!collapsed && (
            <>
              <div className="sidebar-user-meta">
                <div className="sidebar-user-name">{user?.display_name ?? "Пользователь"}</div>
                <div className="sidebar-user-role">{roleLabel}</div>
              </div>
              <button className="icon-button" onClick={logout} title="Выйти">
                <Icon name="logout" />
              </button>
            </>
          )}
        </div>
      </aside>

      <div className="app-main">
        <header className="app-header">
          <button className="icon-button menu-button" onClick={toggle} title="Свернуть меню">
            <Icon name="menu" />
          </button>
          <div className="app-title">
            <h1>{title}</h1>
            {subtitle && <p>{subtitle}</p>}
          </div>
          <div className="header-actions">{actions}</div>
        </header>
        <main className="app-content tf-scroll">{children}</main>
      </div>
    </div>
  );
}

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

export function Card({
  title,
  children,
  id,
  icon,
  actions,
  className = "",
}: {
  title: string;
  children: ReactNode;
  id?: string;
  icon?: string;
  actions?: ReactNode;
  className?: string;
}) {
  return (
    <section className={"card " + className} id={id}>
      <div className="card-heading">
        <h3>{icon && <Icon name={icon} />}{title}</h3>
        {actions && <div className="card-actions">{actions}</div>}
      </div>
      {children}
    </section>
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
