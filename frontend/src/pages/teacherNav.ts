import type { AppNavItem } from "../ui";

export type TeacherNavKey = "summary" | "students" | "lessons" | "assignments" | "finance" | "receipts" | "chat" | "settings";

export function teacherNav(active: TeacherNavKey, badges: Partial<Record<"students" | "lessons" | "assignments" | "receipts", number | string>> = {}): AppNavItem[] {
  return [
    { label: "Сводка", icon: "dashboard", href: "/teacher", active: active === "summary" },
    { label: "Ученики", icon: "group", href: "/teacher/students", badge: badges.students, active: active === "students" },
    { label: "Занятия", icon: "calendar_month", href: "/teacher/lessons", badge: badges.lessons, active: active === "lessons" },
    { label: "Домашние задания", icon: "assignment", href: "/teacher/assignments", badge: badges.assignments, active: active === "assignments" },
    { label: "Финансы", icon: "account_balance_wallet", href: "/teacher/finance", active: active === "finance" },
    { label: "Чеки", icon: "receipt_long", href: "/teacher/receipts", badge: badges.receipts, active: active === "receipts" },
    { label: "Чаты", icon: "chat_bubble", href: "/teacher/chat", active: active === "chat" },
    { label: "Настройки", icon: "settings", href: "/teacher/settings", active: active === "settings" },
  ];
}

export function initials(name?: string): string {
  const parts = (name ?? "").trim().split(/\s+/).filter(Boolean);
  if (parts.length === 0) return "??";
  return parts.slice(0, 2).map((part) => part[0]?.toUpperCase()).join("");
}

export function money(value?: number, currency = "RUB"): string {
  if (typeof value !== "number") return "—";
  return `${Math.round(value)} ${currency}`;
}

export function shortDate(iso?: string): string {
  if (!iso) return "—";
  const date = new Date(iso);
  if (isNaN(date.getTime())) return iso;
  return date.toLocaleDateString("ru-RU", { day: "numeric", month: "short" });
}

export function timeOnly(iso?: string): string {
  if (!iso) return "—";
  const date = new Date(iso);
  if (isNaN(date.getTime())) return iso;
  return date.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" });
}
