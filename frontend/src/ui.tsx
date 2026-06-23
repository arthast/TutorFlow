import { useCallback, useEffect, useState, type ReactNode } from "react";
import { useAuth } from "./auth";

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
