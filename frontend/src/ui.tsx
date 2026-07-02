import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
  type ButtonHTMLAttributes,
  type CSSProperties,
  type FormEvent,
  type InputHTMLAttributes,
  type ReactNode,
  type SelectHTMLAttributes,
  type TextareaHTMLAttributes,
} from "react";
import { Link } from "react-router-dom";
import { api, chat, openFile, type AppNotification, type ChatDialog, type Comment, type FileMeta } from "./api";
import { useAuth } from "./auth";
import { useOnlineStatus, useRealtimeEvent } from "./realtime";

export function Icon({ name, className = "" }: { name: string; className?: string }) {
  return <span className={"ms " + className} aria-hidden="true">{name}</span>;
}

// Инициалы из имени для аватаров (≤2 символа).
export function initials(name?: string): string {
  const parts = (name ?? "TutorFlow").trim().split(/\s+/).filter(Boolean);
  if (parts.length === 0) return "TF";
  return parts.slice(0, 2).map((p) => p[0]?.toUpperCase()).join("");
}

export interface AppNavItem {
  label: string;
  icon: string;
  href: string;
  badge?: number | string;
  active?: boolean;
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
    // На узких экранах дровер всегда стартует закрытым, что бы ни лежало в localStorage.
    if (typeof window !== "undefined" && window.innerWidth < 880) return true;
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
      {/* Затемнение под мобильным дровером; на десктопе скрыто через CSS */}
      {!collapsed && <div className="sidebar-backdrop" aria-hidden="true" onClick={() => setCollapsed(true)} />}
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
              <button className="icon-button" onClick={logout} title="Выйти" aria-label="Выйти из аккаунта">
                <Icon name="logout" />
              </button>
            </>
          )}
        </div>
      </aside>

      <div className="app-main">
        <header className="app-header">
          <button
            className="icon-button menu-button"
            onClick={toggle}
            title={collapsed ? "Открыть меню" : "Свернуть меню"}
            aria-label={collapsed ? "Открыть меню" : "Свернуть меню"}
            aria-expanded={!collapsed}
          >
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

export function Card({
  title,
  children,
  id,
  icon,
  actions,
  className = "",
}: {
  title: ReactNode;
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

// Единый язык статусов на весь продукт (lesson / assignment / receipt / finance).
const PILL_CLASS: Record<string, string> = {
  // занятие
  scheduled: "pill-info",
  completed: "pill-success",
  cancelled: "pill-muted",
  // связь с учеником
  active: "pill-success",
  invited: "pill-muted",
  archived: "pill-muted",
  // домашка
  assigned: "pill-info",
  in_progress: "pill-info",
  submitted: "pill-warning",
  reviewed: "pill-success",
  needs_fix: "pill-warning",
  accepted: "pill-success",
  done: "pill-success",
  draft: "pill-muted",
  expired: "pill-danger",
  overdue: "pill-danger",
  // чек
  pending_review: "pill-warning",
  pending: "pill-warning",
  confirmed: "pill-success",
  rejected: "pill-danger",
  // финансы (журнал операций)
  charge: "pill-muted",
  payment: "pill-success",
  paid: "pill-success",
  refund: "pill-info",
  correction: "pill-muted",
};

const PILL_LABEL: Record<string, string> = {
  scheduled: "запланировано",
  completed: "завершено",
  cancelled: "отменено",
  active: "активен",
  invited: "приглашён",
  archived: "архив",
  assigned: "выдано",
  in_progress: "в работе",
  submitted: "сдано",
  reviewed: "проверено",
  needs_fix: "нужны правки",
  accepted: "принято",
  done: "выполнено",
  draft: "черновик",
  expired: "просрочено",
  overdue: "просрочено",
  pending_review: "на проверке",
  pending: "ожидает",
  confirmed: "подтверждён",
  rejected: "отклонён",
  charge: "начисление",
  payment: "оплата",
  paid: "оплачено",
  refund: "возврат",
  correction: "коррекция",
};

export function StatusPill({ status, label }: { status: string; label?: string }) {
  return (
    <span className={"pill " + (PILL_CLASS[status] ?? "pill-muted")}>
      {label ?? PILL_LABEL[status] ?? status}
    </span>
  );
}

// ====== Кнопка: варианты + размеры + иконка/лоадер ======
type ButtonVariant = "primary" | "secondary" | "ghost" | "danger";
type ButtonSize = "sm" | "md" | "lg";

const BTN_VARIANT: Record<ButtonVariant, string> = {
  primary: "primary",
  secondary: "secondary",
  ghost: "ghost",
  danger: "danger",
};
const BTN_SIZE: Record<ButtonSize, string> = { sm: "small", md: "", lg: "lg" };

export function Button({
  variant = "secondary",
  size = "md",
  icon,
  loading = false,
  block = false,
  className = "",
  children,
  type = "button",
  disabled,
  ...rest
}: {
  variant?: ButtonVariant;
  size?: ButtonSize;
  icon?: string;
  loading?: boolean;
  block?: boolean;
} & ButtonHTMLAttributes<HTMLButtonElement>) {
  const hasIcon = !!icon || loading;
  const cls = [
    BTN_VARIANT[variant],
    BTN_SIZE[size],
    block ? "block" : "",
    hasIcon ? "has-icon" : "",
    className,
  ].filter(Boolean).join(" ");
  return (
    <button type={type} className={cls} disabled={disabled || loading} {...rest}>
      {loading ? <Icon name="progress_activity" className="spin" /> : icon && <Icon name={icon} />}
      {children}
    </button>
  );
}

// ====== Поля ввода ======
export function Field({
  label,
  hint,
  error,
  children,
  className = "",
}: {
  label?: ReactNode;
  hint?: string;
  error?: string | null;
  children: ReactNode;
  className?: string;
}) {
  return (
    <div className={"field " + className}>
      {label !== undefined ? <label>{label}{children}</label> : children}
      {error ? <div className="field-error">{error}</div> : hint ? <div className="hint">{hint}</div> : null}
    </div>
  );
}

export function Input({ invalid, className = "", ...rest }: { invalid?: boolean } & InputHTMLAttributes<HTMLInputElement>) {
  return <input className={(invalid ? "invalid " : "") + className} {...rest} />;
}

export function Textarea({ invalid, className = "", ...rest }: { invalid?: boolean } & TextareaHTMLAttributes<HTMLTextAreaElement>) {
  return <textarea className={(invalid ? "invalid " : "") + className} {...rest} />;
}

export function Select({
  invalid,
  className = "",
  children,
  ...rest
}: { invalid?: boolean } & SelectHTMLAttributes<HTMLSelectElement>) {
  return (
    <span className="select-wrap">
      <select className={(invalid ? "invalid " : "") + className} {...rest}>{children}</select>
      <Icon name="expand_more" />
    </span>
  );
}

// ====== Бейджи и счётчики ======
export function Badge({
  tone = "muted",
  icon,
  children,
}: {
  tone?: "muted" | "info" | "success" | "warning" | "danger";
  icon?: string;
  children: ReactNode;
}) {
  return (
    <span className={"badge badge-" + tone}>
      {icon && <Icon name={icon} />}
      {children}
    </span>
  );
}

export function Counter({
  value,
  tone = "accent",
}: {
  value?: number | string | null;
  tone?: "accent" | "danger" | "warning" | "muted";
}) {
  if (value === undefined || value === null || value === 0 || value === "0") return null;
  return <span className={"counter counter-" + tone}>{value}</span>;
}

// ====== Аватар + индикатор присутствия ======
export function Avatar({
  name,
  tone,
  size = "md",
  presence,
}: {
  name?: string;
  tone?: "teacher" | "student" | "amber" | "muted";
  size?: "sm" | "md" | "lg";
  presence?: "online" | "away";
}) {
  const sizeCls = size === "sm" ? " avatar-sm" : size === "lg" ? " avatar-lg" : "";
  const avatar = (
    <div className={"avatar" + (tone ? " tone-" + tone : "") + sizeCls}>{initials(name)}</div>
  );
  if (!presence) return avatar;
  return (
    <span className="avatar-wrap">
      {avatar}
      <span className={"avatar-online" + (presence === "away" ? " away" : "")} />
    </span>
  );
}

// ====== Метрика (вынесена из Teacher.tsx, переиспользуема) ======
export function Metric({
  icon,
  label,
  value,
  sub,
  subTone,
  pill,
  tone,
}: {
  icon: string;
  label: string;
  value: number | string;
  sub?: ReactNode;
  subTone?: "pos";
  pill?: { label: string; tone?: "warning" | "success" | "info" };
  tone?: "warn";
}) {
  return (
    <div className={"metric" + (tone ? " " + tone : "")}>
      <div className="label"><Icon name={icon} />{label}</div>
      {pill ? (
        <div className="metric-value-row">
          <div className="value">{value}</div>
          <span className={"metric-pill " + (pill.tone ?? "warning")}>{pill.label}</span>
        </div>
      ) : (
        <div className="value">{value}</div>
      )}
      {sub !== undefined && <div className={"metric-sub" + (subTone ? " " + subTone : "")}>{sub}</div>}
    </div>
  );
}

// ====== Строка списка дашборда (Сегодня / Требует внимания / Сообщения) ======
export function ListRow({
  time,
  leading,
  title,
  subtitle,
  className = "",
  onClick,
  children,
}: {
  time?: ReactNode;
  leading?: ReactNode;
  title: ReactNode;
  subtitle?: ReactNode;
  className?: string;
  onClick?: () => void;
  children?: ReactNode;
}) {
  return (
    <div
      className={"dash-row " + (onClick ? "clickable " : "") + className}
      onClick={onClick}
      role={onClick ? "button" : undefined}
      tabIndex={onClick ? 0 : undefined}
      onKeyDown={
        onClick
          ? (e) => {
            if (e.key === "Enter" || e.key === " ") {
              e.preventDefault();
              onClick();
            }
          }
          : undefined
      }
    >
      {time !== undefined && <div className="dash-time">{time}</div>}
      {leading}
      <div className="dash-main">
        <div className="t">{title}</div>
        {subtitle !== undefined && <div className="s">{subtitle}</div>}
      </div>
      {children}
    </div>
  );
}

// ====== Пустое состояние (общий) ======
export function EmptyState({
  icon = "inbox",
  title,
  hint,
  tone,
  action,
}: {
  icon?: string;
  title: string;
  hint?: string;
  tone?: "success";
  action?: ReactNode;
}) {
  return (
    <div className={"empty-state" + (tone ? " " + tone : "")}>
      <Icon name={icon} />
      <strong>{title}</strong>
      {hint && <p>{hint}</p>}
      {action}
    </div>
  );
}

// ====== Скелетон-лоадер ======
export function Skeleton({
  width,
  height = 12,
  radius,
  className = "",
}: {
  width?: number | string;
  height?: number | string;
  radius?: number | string;
  className?: string;
}) {
  const style: CSSProperties = { width, height, borderRadius: radius };
  return <span className={"skeleton " + className} style={style} aria-hidden="true" />;
}

// Несколько строк-плейсхолдеров для карточек-списков.
export function SkeletonRows({ count = 3 }: { count?: number }) {
  return (
    <>
      {Array.from({ length: count }).map((_, i) => (
        <div className="skeleton-row" key={i}>
          <Skeleton className="skeleton-avatar" width={38} height={38} radius={10} />
          <div className="skeleton-lines">
            <Skeleton width="55%" height={12} />
            <Skeleton width="35%" height={10} />
          </div>
        </div>
      ))}
    </>
  );
}

// ====== Вкладки и сегменты ======
export interface TabItem {
  key: string;
  label: ReactNode;
  count?: number | string;
}

export function Tabs({
  items,
  active,
  onChange,
  right,
}: {
  items: TabItem[];
  active: string;
  onChange: (key: string) => void;
  right?: ReactNode;
}) {
  return (
    <div className="page-tabs-bar">
      <div className="page-tabs">
        {items.map((it) => (
          <button key={it.key} type="button" className={active === it.key ? "active" : ""} onClick={() => onChange(it.key)}>
            {it.label}
            {it.count !== undefined && it.count !== null && <span>{it.count}</span>}
          </button>
        ))}
      </div>
      {right}
    </div>
  );
}

export function Segmented({
  items,
  active,
  onChange,
}: {
  items: TabItem[];
  active: string;
  onChange: (key: string) => void;
}) {
  return (
    <div className="segmented">
      {items.map((it) => (
        <button key={it.key} type="button" className={active === it.key ? "active" : ""} onClick={() => onChange(it.key)}>
          {it.label}
          {it.count !== undefined && it.count !== null && <span className="tab-count">{it.count}</span>}
        </button>
      ))}
    </div>
  );
}

// ====== Модальное окно ======
export function Modal({
  title,
  subtitle,
  onClose,
  onSubmit,
  wide = false,
  children,
  footer,
}: {
  title: ReactNode;
  subtitle?: ReactNode;
  onClose: () => void;
  onSubmit?: () => void;
  wide?: boolean;
  children: ReactNode;
  footer?: ReactNode;
}) {
  const panelRef = useRef<HTMLElement | null>(null);

  // Esc закрывает; Tab зациклен внутри панели; фокус возвращается инициатору.
  useEffect(() => {
    const opener = document.activeElement as HTMLElement | null;
    const panel = panelRef.current;

    function focusables(): HTMLElement[] {
      if (!panel) return [];
      return Array.from(
        panel.querySelectorAll<HTMLElement>(
          'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])',
        ),
      ).filter((el) => !el.hasAttribute("disabled") && el.offsetParent !== null);
    }

    // Начальный фокус: autoFocus-поле, иначе первое поле формы, иначе первая кнопка.
    if (panel && !panel.contains(document.activeElement)) {
      const list = focusables();
      const field = list.find((el) => ["INPUT", "SELECT", "TEXTAREA"].includes(el.tagName));
      (field ?? list[0])?.focus();
    }

    function onKey(e: KeyboardEvent) {
      if (e.key === "Escape") {
        onClose();
        return;
      }
      if (e.key !== "Tab") return;
      const list = focusables();
      if (list.length === 0) return;
      const first = list[0];
      const last = list[list.length - 1];
      const active = document.activeElement;
      if (e.shiftKey && (active === first || !panel?.contains(active))) {
        e.preventDefault();
        last.focus();
      } else if (!e.shiftKey && active === last) {
        e.preventDefault();
        first.focus();
      }
    }
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("keydown", onKey);
      opener?.focus?.();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [onClose]);

  const panelClass = "modal-panel" + (wide ? " modal-panel-wide" : "");
  const heading = (
    <>
      <div className="modal-heading">
        <div>
          <h2>{title}</h2>
          {subtitle !== undefined && <p>{subtitle}</p>}
        </div>
        <button className="icon-button" type="button" onClick={onClose} title="Закрыть" aria-label="Закрыть окно">
          <Icon name="close" />
        </button>
      </div>
      {children}
      {footer && <div className="modal-actions">{footer}</div>}
    </>
  );

  return (
    <div className="modal-overlay" onMouseDown={onClose} role="dialog" aria-modal="true">
      {onSubmit ? (
        <form
          className={panelClass}
          ref={(el) => { panelRef.current = el; }}
          onMouseDown={(e) => e.stopPropagation()}
          onSubmit={(e: FormEvent) => {
            e.preventDefault();
            onSubmit();
          }}
        >
          {heading}
        </form>
      ) : (
        <div
          className={panelClass}
          ref={(el) => { panelRef.current = el; }}
          onMouseDown={(e) => e.stopPropagation()}
        >
          {heading}
        </div>
      )}
    </div>
  );
}

// ====== Тосты: провайдер + хук ======
export type ToastTone = "info" | "success" | "warning" | "danger";
export interface ToastInput {
  tone?: ToastTone;
  icon?: string;
  title: string;
  body?: string;
}
interface ToastData extends Required<Pick<ToastInput, "title">> {
  id: number;
  tone: ToastTone;
  icon: string;
  body?: string;
}

const TONE_ICON: Record<ToastTone, string> = {
  info: "info",
  success: "check_circle",
  warning: "warning",
  danger: "cancel",
};

const ToastCtx = createContext<(t: ToastInput) => void>(() => {});

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<ToastData[]>([]);
  const seq = useRef(0);

  const dismiss = useCallback((id: number) => {
    setToasts((list) => list.filter((t) => t.id !== id));
  }, []);

  const push = useCallback(
    (t: ToastInput) => {
      const id = ++seq.current;
      const tone = t.tone ?? "info";
      const next: ToastData = { id, tone, icon: t.icon ?? TONE_ICON[tone], title: t.title, body: t.body };
      setToasts((list) => [next, ...list].slice(0, 4));
      setTimeout(() => dismiss(id), 5000);
    },
    [dismiss],
  );

  return (
    <ToastCtx.Provider value={push}>
      {children}
      <div className="toast-stack" aria-live="polite">
        {toasts.map((t) => (
          <div className={"toast " + t.tone} key={t.id}>
            <Icon name={t.icon} />
            <div className="toast-main">
              <div className="toast-title">{t.title}</div>
              {t.body && <div className="toast-body">{t.body}</div>}
            </div>
            <button className="toast-close icon-button" type="button" onClick={() => dismiss(t.id)} title="Закрыть" aria-label="Закрыть уведомление">
              <Icon name="close" />
            </button>
          </div>
        ))}
      </div>
    </ToastCtx.Provider>
  );
}

export function useToast() {
  return useContext(ToastCtx);
}

// Ненавязчивые тосты на входящие realtime-события (notification + chat.message).
// Дедуп по id + троттлинг, чтобы не спамить; сообщения не тостим на самой странице чата.
export function RealtimeToasts() {
  const toast = useToast();
  const { user } = useAuth();
  const selfId = user?.user_id ?? "";
  const seen = useRef<Set<string>>(new Set());
  const lastAt = useRef(0);

  useRealtimeEvent((event) => {
    const now = Date.now();
    if (now - lastAt.current < 1500) return; // троттлинг

    if (event.type === "notification") {
      const id = "n:" + String(event.payload.notification_id ?? event.payload.id ?? event.seq);
      if (seen.current.has(id)) return;
      seen.current.add(id);
      lastAt.current = now;
      toast({
        tone: "info",
        icon: notifIcon(String(event.payload.type ?? "notification")),
        title: String(event.payload.title ?? "Новое уведомление"),
        body: String(event.payload.body ?? ""),
      });
      return;
    }

    if (event.type === "chat.message") {
      const senderId = String(event.payload.sender_id ?? "");
      if (senderId && senderId === selfId) return; // своё сообщение не тостим
      // не тостим, когда пользователь уже в чате
      if (typeof window !== "undefined" && window.location.pathname.includes("/chat")) return;
      const id = "m:" + String(event.payload.message_id ?? event.payload.id ?? event.seq);
      if (seen.current.has(id)) return;
      seen.current.add(id);
      lastAt.current = now;
      const preview = String(event.payload.text ?? "");
      toast({
        tone: "info",
        icon: "chat_bubble",
        title: String(event.payload.sender_name ?? "Новое сообщение"),
        body: preview || "Вложение",
      });
    }
  }, [selfId]);

  return null;
}

export function ErrorMsg({ error }: { error: string | null }) {
  if (!error) return null;
  return <div className="error">{error}</div>;
}

// Единообразная ошибка списка/карточки: сообщение + «Повторить».
export function ErrorState({ error, onRetry }: { error: string; onRetry?: () => void }) {
  return (
    <EmptyState
      icon="error"
      title="Не удалось загрузить"
      hint={error}
      action={onRetry && <Button size="sm" icon="refresh" onClick={onRetry}>Повторить</Button>}
    />
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

// ====== Дашборд: живые уведомления (общий для обеих ролей) ======
const NOTIF_ICON: Record<string, string> = {
  receipt: "receipt_long",
  payment: "receipt_long",
  lesson: "calendar_month",
  assignment: "assignment",
  submission: "assignment_turned_in",
  review: "grading",
  chat: "chat_bubble",
  message: "chat_bubble",
};

function notifIcon(type: string): string {
  const key = Object.keys(NOTIF_ICON).find((k) => type.includes(k));
  return key ? NOTIF_ICON[key] : "notifications";
}

export function NotificationsPanel() {
  const toast = useToast();
  const notifications = useAsync<AppNotification[]>(() => api.get("/notifications"), []);
  const [busy, setBusy] = useState(false);
  const items = notifications.data ?? [];
  const unread = items.filter((n) => !n.is_read).length;

  useRealtimeEvent((event) => {
    if (event.type === "notification") notifications.reload();
  }, [notifications.reload]);

  async function markAll() {
    setBusy(true);
    try {
      await Promise.all(items.filter((n) => !n.is_read).map((n) => api.post(`/notifications/${n.id}/read`)));
      notifications.reload();
    } catch (err) {
      toast({ tone: "danger", title: "Ошибка", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  async function markOne(id: string) {
    try {
      await api.post(`/notifications/${id}/read`);
      notifications.reload();
    } catch {
      /* mark-read не критичен */
    }
  }

  return (
    <Card
      title={
        <>
          <span className="live-dot" />
          Уведомления
          <Counter value={unread} tone="danger" />
        </>
      }
      actions={
        unread > 0 ? (
          <button className="card-head-link" type="button" disabled={busy} onClick={markAll}>Прочитать всё</button>
        ) : undefined
      }
    >
      {notifications.loading && !notifications.data ? (
        <SkeletonRows count={3} />
      ) : notifications.error ? (
        <ErrorState error={notifications.error} onRetry={notifications.reload} />
      ) : items.length === 0 ? (
        <EmptyState icon="notifications" title="Новых уведомлений нет" />
      ) : (
        <div className="card-scroll">
          {items.slice(0, 8).map((n) => (
            <ListRow
              key={n.id}
              className={n.is_read ? "dim" : ""}
              onClick={n.is_read ? undefined : () => markOne(n.id)}
              leading={<span className="dash-icon"><Icon name={notifIcon(n.type)} /></span>}
              title={n.title}
              subtitle={n.body || fmtDate(n.created_at)}
            >
              {!n.is_read && <span className="unread-dot" />}
            </ListRow>
          ))}
        </div>
      )}
    </Card>
  );
}

// ====== Дашборд: сообщения (общий) ======
export function MessagesCard({
  contacts,
  chatHref,
}: {
  contacts: { id: string; name: string }[];
  chatHref: string;
}) {
  const { user } = useAuth();
  const selfId = user?.user_id ?? "";
  const dialogs = useAsync<ChatDialog[]>(() => chat.listDialogs(), []);

  useRealtimeEvent((event) => {
    if (event.type.startsWith("chat")) dialogs.reload();
  }, [dialogs.reload]);

  const nameOf = (id: string) => contacts.find((c) => c.id === id)?.name ?? id.slice(0, 8);
  const peerId = (d: ChatDialog) => (d.teacher_id === selfId ? d.student_id : d.teacher_id);

  const list = (dialogs.data ?? [])
    .slice()
    .sort((a, b) => new Date(b.last_message_at ?? 0).getTime() - new Date(a.last_message_at ?? 0).getTime())
    .slice(0, 4);

  return (
    <Card title="Сообщения" icon="forum" actions={<Link className="card-head-link" to={chatHref}>Открыть чат</Link>}>
      {dialogs.loading && !dialogs.data ? (
        <SkeletonRows count={3} />
      ) : dialogs.error ? (
        <ErrorState error={dialogs.error} onRetry={dialogs.reload} />
      ) : list.length === 0 ? (
        <EmptyState icon="forum" title="Диалогов пока нет" hint="Начните переписку на странице «Чат»." />
      ) : (
        list.map((d, i) => (
          <MessageRow key={d.id} dialog={d} peerId={peerId(d)} name={nameOf(peerId(d))} tone={MSG_TONES[i % MSG_TONES.length]} chatHref={chatHref} />
        ))
      )}
    </Card>
  );
}

const MSG_TONES = ["teacher", "student", "amber", "muted"] as const;

function MessageRow({
  dialog,
  peerId,
  name,
  tone,
  chatHref,
}: {
  dialog: ChatDialog;
  peerId: string;
  name: string;
  tone: "teacher" | "student" | "amber" | "muted";
  chatHref: string;
}) {
  const online = useOnlineStatus(peerId);
  const preview = dialog.last_message?.text || (dialog.last_message ? "вложение" : "нет сообщений");
  return (
    <Link className="dash-row clickable" to={chatHref} style={{ textDecoration: "none", color: "inherit" }}>
      <Avatar name={name} tone={tone} presence={online ? "online" : "away"} />
      <div className="dash-main">
        <div className="t">{name}</div>
        <div className="s">{preview}</div>
      </div>
      <Counter value={dialog.unread_count} tone="accent" />
    </Link>
  );
}

// ====== Тред комментариев (преподаватель + ученик) ======
export function CommentThread({
  comments,
  teacherId,
  selfId,
  onSubmit,
}: {
  comments: Comment[];
  teacherId?: string;
  selfId?: string;
  onSubmit: (text: string) => Promise<void>;
}) {
  const [text, setText] = useState("");
  const [busy, setBusy] = useState(false);

  async function send() {
    const value = text.trim();
    if (!value) return;
    setBusy(true);
    try {
      await onSubmit(value);
      setText("");
    } finally {
      setBusy(false);
    }
  }

  return (
    <div>
      <div className="comment-thread">
        {comments.map((c) => {
          const isTeacher = c.author_id === teacherId;
          const who = c.author_id === selfId ? "Вы" : isTeacher ? "Преподаватель" : "Ученик";
          return (
            <div className="comment-item" key={c.id}>
              <Avatar name={isTeacher ? "Преподаватель" : "Ученик"} tone={isTeacher ? "teacher" : "student"} size="sm" />
              <div className="comment-body">
                <div className="comment-head">
                  <span className={"comment-author " + (isTeacher ? "teacher" : "student")}>{who}</span>
                  <span className="comment-time">{fmtDate(c.created_at)}</span>
                </div>
                <div className="comment-text">{c.text}</div>
              </div>
            </div>
          );
        })}
        {comments.length === 0 && <p className="hint">Комментариев пока нет.</p>}
      </div>
      <div className="comment-form">
        <textarea
          placeholder="Написать комментарий…"
          aria-label="Текст комментария"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) send();
          }}
        />
        <Button variant="primary" icon="send" loading={busy} disabled={!text.trim()} onClick={send}>
          Отправить
        </Button>
      </div>
    </div>
  );
}

// ====== Пароль: поле с показать/скрыть + индикатор надёжности ======
export function PasswordInput({
  value,
  onChange,
  id,
  placeholder,
  icon = "lock",
  autoComplete,
  required,
  minLength,
  invalid,
}: {
  value: string;
  onChange: (value: string) => void;
  id?: string;
  placeholder?: string;
  icon?: string;
  autoComplete?: string;
  required?: boolean;
  minLength?: number;
  invalid?: boolean;
}) {
  const [show, setShow] = useState(false);
  return (
    <div className="input-with-icon pw-field">
      <Icon name={icon} />
      <input
        id={id}
        type={show ? "text" : "password"}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
        autoComplete={autoComplete}
        required={required}
        minLength={minLength}
        className={invalid ? "invalid" : ""}
      />
      <button
        type="button"
        className="pw-toggle"
        onClick={() => setShow((s) => !s)}
        title={show ? "Скрыть пароль" : "Показать пароль"}
        aria-label={show ? "Скрыть пароль" : "Показать пароль"}
      >
        <Icon name={show ? "visibility_off" : "visibility"} />
      </button>
    </div>
  );
}

// 0 — пусто, 1 — слабый, 2 — средний, 3 — надёжный.
export function passwordStrength(pw: string): number {
  if (!pw) return 0;
  let n = 0;
  if (pw.length >= 8) n += 1;
  if (/[a-zа-яё]/.test(pw) && /[A-ZА-ЯЁ]/.test(pw)) n += 1;
  if (/[0-9]/.test(pw) || /[^A-Za-zА-Яа-яЁё0-9]/.test(pw)) n += 1;
  return Math.min(n, 3);
}

const STRENGTH = [
  { label: "Используйте буквы, цифры и регистр", cls: "" },
  { label: "Слабый пароль", cls: "s1" },
  { label: "Средний пароль", cls: "s2" },
  { label: "Надёжный пароль", cls: "s3" },
];

export function PasswordStrength({ value }: { value: string }) {
  const score = passwordStrength(value);
  const meta = STRENGTH[value ? score : 0];
  return (
    <div className={"pw-strength " + meta.cls}>
      <div className="pw-strength-bars"><span /><span /><span /></div>
      <div className="pw-strength-label">{meta.label}</div>
    </div>
  );
}

// ====== Общая карточка смены пароля (Settings, teacher + student) ======
export function ChangePasswordCard() {
  const toast = useToast();
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [confirm, setConfirm] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const mismatch = confirm.length > 0 && confirm !== next;
  const canSubmit = current.length > 0 && next.length >= 8 && next === confirm;

  async function submit(e: FormEvent) {
    e.preventDefault();
    setError(null);
    if (next.length < 8) {
      setError("Новый пароль — минимум 8 символов.");
      return;
    }
    if (next !== confirm) {
      setError("Пароли не совпадают.");
      return;
    }
    setBusy(true);
    try {
      await api.post("/auth/change-password", { current_password: current, new_password: next });
      toast({ tone: "success", title: "Пароль обновлён", body: "Используйте новый пароль при следующем входе." });
      setCurrent("");
      setNext("");
      setConfirm("");
    } catch (err) {
      setError((err as Error).message);
      toast({ tone: "danger", title: "Не удалось сменить пароль", body: (err as Error).message });
    } finally {
      setBusy(false);
    }
  }

  return (
    <Card title="Смена пароля" icon="lock_reset">
      <p className="card-note">Если вы вошли по временному паролю — задайте здесь постоянный.</p>
      <ErrorMsg error={error} />
      <form onSubmit={submit}>
        <Field label="Текущий пароль">
          <PasswordInput value={current} onChange={setCurrent} autoComplete="current-password" required />
        </Field>
        <Field label="Новый пароль">
          <PasswordInput value={next} onChange={setNext} autoComplete="new-password" minLength={8} required />
        </Field>
        {next.length > 0 && <PasswordStrength value={next} />}
        <Field label="Повторите новый пароль" error={mismatch ? "Пароли не совпадают" : null}>
          <PasswordInput value={confirm} onChange={setConfirm} autoComplete="new-password" invalid={mismatch} required />
        </Field>
        <Button variant="primary" type="submit" icon="lock_reset" loading={busy} disabled={!canSubmit} className="auth-submit">
          Обновить пароль
        </Button>
      </form>
    </Card>
  );
}

export function fmtDate(iso?: string): string {
  if (!iso) return "";
  const d = new Date(iso);
  if (isNaN(d.getTime())) return iso;
  return d.toLocaleString("ru-RU", { dateStyle: "medium", timeStyle: "short" });
}

// ====== Деньги: единый формат на весь продукт ======
// «15 000 ₽» для RUB, иначе «15 000 XXX»; «—» если значения нет.
export function money(value?: number, currency = "RUB"): string {
  if (typeof value !== "number" || !Number.isFinite(value)) return "—";
  const symbol = currency === "RUB" ? "₽" : currency;
  return `${Math.round(value).toLocaleString("ru-RU")} ${symbol}`;
}

// Со знаком: минус для отрицательных (переплата), без плюса.
export function signedMoney(value?: number, currency = "RUB"): string {
  if (typeof value !== "number" || !Number.isFinite(value)) return "—";
  if (value === 0) return money(0, currency);
  return `${value < 0 ? "−" : ""}${money(Math.abs(value), currency)}`;
}
