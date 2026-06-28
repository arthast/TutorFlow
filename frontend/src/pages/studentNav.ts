import type { AppNavItem } from "../ui";

export type StudentNavKey = "summary" | "lessons" | "assignments" | "payments" | "receipts" | "chat" | "settings";

export function studentNav(
  active: StudentNavKey,
  badges: Partial<Record<"lessons" | "assignments" | "receipts", number | string>> = {},
): AppNavItem[] {
  return [
    { label: "Главная", icon: "dashboard", href: "/student", active: active === "summary" },
    { label: "Мои занятия", icon: "calendar_month", href: "/student/lessons", badge: badges.lessons, active: active === "lessons" },
    { label: "Домашние задания", icon: "assignment", href: "/student/assignments", badge: badges.assignments, active: active === "assignments" },
    { label: "Оплата", icon: "payments", href: "/student/payments", active: active === "payments" },
    { label: "Мои чеки", icon: "receipt_long", href: "/student/receipts", badge: badges.receipts, active: active === "receipts" },
    { label: "Чат", icon: "chat_bubble", href: "/student/chat", active: active === "chat" },
    { label: "Настройки", icon: "settings", href: "/student/settings", active: active === "settings" },
  ];
}

export function money(value?: number, currency = "RUB"): string {
  if (typeof value !== "number") return "-";
  return `${Math.round(value)} ${currency}`;
}

export function lessonInterval(startsAt?: string, endsAt?: string): string {
  if (!startsAt) return "-";
  const start = new Date(startsAt);
  if (isNaN(start.getTime())) return startsAt;
  const startText = start.toLocaleDateString("ru-RU", {
    day: "numeric",
    month: "short",
    hour: "2-digit",
    minute: "2-digit",
  });
  if (!endsAt) return startText;
  const end = new Date(endsAt);
  if (isNaN(end.getTime())) return `${startText} - ${endsAt}`;
  return `${startText} - ${end.toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" })}`;
}
