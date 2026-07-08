// Тонкий клиент к api-gateway. Единственная точка входа; во внутренние сервисы не ходим.

const BASE_URL: string =
  (import.meta.env.VITE_API_URL as string | undefined) ?? "http://localhost:8080";

const TOKEN_KEY = "tutorflow_token";
const FALLBACK_ERROR_MESSAGE = "Что-то пошло не так. Попробуйте ещё раз.";

const RU_BY_CODE: Record<string, string> = {
  validation_error: "Проверьте заполненные поля",
  unauthorized: "Сессия истекла — войдите заново",
  forbidden: "Нет доступа к этому действию",
  not_found: "Не найдено — возможно, объект уже удалён",
  conflict: "Действие уже выполнено или конфликтует с текущим состоянием",
  payload_too_large: "Файл слишком большой",
  unsupported_media_type: "Неподдерживаемый формат",
  business_rule: "Действие недоступно по правилам платформы",
  internal_error: "Ошибка на сервере. Попробуйте позже",
  network_error: "Нет соединения с сервером. Проверьте интернет",
};

const RU_CONTEXT_MESSAGES = [
  {
    method: "POST",
    path: "/assignments/:id/submit",
    code: "conflict",
    message: "Задание уже закрыто — сдать решение нельзя",
  },
  {
    method: "POST",
    path: "/auth/login",
    code: "unauthorized",
    message: "Неверный email или пароль",
  },
  {
    method: "POST",
    path: "/auth/change-password",
    code: "unauthorized",
    message: "Неверный текущий пароль",
  },
  {
    method: "POST",
    path: "/students",
    code: "conflict",
    message: "Ученик с таким email уже существует",
  },
  {
    method: "POST",
    path: "/files",
    code: "payload_too_large",
    message: "Файл слишком большой (лимит 10 МБ)",
  },
];

function matchesPath(pattern: string, path: string): boolean {
  const patternParts = pattern.split("/");
  const pathParts = path.split("?")[0].split("/");
  if (patternParts.length !== pathParts.length) return false;
  return patternParts.every((part, index) => part.startsWith(":") || part === pathParts[index]);
}

function messageFor(status: number, method: string, path: string, code: string): string {
  const context = RU_CONTEXT_MESSAGES.find(
    (item) => item.method === method && item.code === code && matchesPath(item.path, path),
  );

  if (context) return context.message;
  if (RU_BY_CODE[code]) return RU_BY_CODE[code];
  if (status === 413) return "Файл слишком большой";
  if (status === 401) return RU_BY_CODE.unauthorized;
  if (status >= 500) return RU_BY_CODE.internal_error;
  return FALLBACK_ERROR_MESSAGE;
}

export function getToken(): string | null {
  return localStorage.getItem(TOKEN_KEY);
}
export function setToken(token: string): void {
  localStorage.setItem(TOKEN_KEY, token);
}
export function clearToken(): void {
  localStorage.removeItem(TOKEN_KEY);
}

export class ApiError extends Error {
  code: string;
  status: number;
  raw: string;

  constructor(status: number, code: string, raw: string, method = "", path = "") {
    super(messageFor(status, method, path, code));
    this.code = code;
    this.status = status;
    this.raw = raw;
    console.debug("ApiError", { status, code, raw, method, path });
  }
}

async function parse(res: Response): Promise<unknown> {
  const text = await res.text();
  if (!text) return {};
  try {
    return JSON.parse(text);
  } catch {
    return text;
  }
}

async function request<T>(
  method: string,
  path: string,
  opts: { body?: unknown; auth?: boolean; form?: FormData } = {},
): Promise<T> {
  const headers: Record<string, string> = {};
  const token = getToken();
  if (opts.auth !== false && token) headers["Authorization"] = "Bearer " + token;

  let body: BodyInit | undefined;
  if (opts.form) {
    body = opts.form; // multipart — content-type выставит браузер
  } else if (opts.body !== undefined) {
    headers["Content-Type"] = "application/json";
    body = JSON.stringify(opts.body);
  }

  let res: Response;
  try {
    res = await fetch(BASE_URL + path, { method, headers, body });
  } catch (err) {
    throw new ApiError(0, "network_error", (err as Error).message, method, path);
  }
  const data = await parse(res);

  if (!res.ok) {
    const env = data as { error?: { code?: string; message?: string } };
    const code = env?.error?.code ?? "error";
    const raw = env?.error?.message ?? `HTTP ${res.status}`;
    throw new ApiError(res.status, code, raw, method, path);
  }
  return data as T;
}

async function requestBlob(path: string): Promise<Blob> {
  const token = getToken();
  const headers: Record<string, string> = {};
  if (token) headers["Authorization"] = "Bearer " + token;
  let res: Response;
  try {
    res = await fetch(BASE_URL + path, { headers });
  } catch (err) {
    throw new ApiError(0, "network_error", (err as Error).message, "GET", path);
  }
  if (!res.ok) {
    const data = await parse(res);
    const env = data as { error?: { code?: string; message?: string } };
    const code = env?.error?.code ?? "error";
    const raw = env?.error?.message ?? `HTTP ${res.status}`;
    throw new ApiError(res.status, code, raw, "GET", path);
  }
  return res.blob();
}

export const api = {
  get: <T>(path: string) => request<T>("GET", path),
  post: <T>(path: string, body?: unknown) => request<T>("POST", path, { body }),
  postPublic: <T>(path: string, body?: unknown) =>
    request<T>("POST", path, { body, auth: false }),
  upload: <T>(path: string, form: FormData) => request<T>("POST", path, { form }),
  getBlob: (path: string) => requestBlob(path),
};

export const reports = {
  teacherDashboard: () => api.get<TeacherDashboard>("/dashboard/teacher"),
  studentDashboard: () => api.get<StudentDashboard>("/dashboard/student"),
  studentSummary: (studentId: string) =>
    api.get<StudentSummary>(`/students/${studentId}/summary`),
};

// Чат (5J): личная переписка teacher<->student, без realtime (polling).
export const chat = {
  listDialogs: () => api.get<ChatDialog[]>("/chats"),
  createDialog: (otherUserId: string) =>
    api.post<ChatDialog>("/chats", { other_user_id: otherUserId }),
  listMessages: (dialogId: string) =>
    api.get<ChatMessage[]>(`/chats/${dialogId}/messages`),
  sendMessage: (dialogId: string, text: string, fileIds?: string[]) =>
    api.post<ChatMessage>(`/chats/${dialogId}/messages`, {
      text,
      file_ids: fileIds && fileIds.length ? fileIds : undefined,
    }),
  markRead: (dialogId: string, upToMessageId: string) =>
    api.post<ChatReadMarker>(`/chats/${dialogId}/read`, {
      up_to_message_id: upToMessageId,
    }),
};

// Открыть файл (чек/вложение) в новой вкладке с авторизацией.
export async function openFile(fileId: string): Promise<void> {
  const blob = await api.getBlob(`/files/${fileId}/download`);
  const url = URL.createObjectURL(blob);
  window.open(url, "_blank", "noopener");
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}

// ---- типы (по docs/api-contracts/gateway.openapi.yaml) ----

export interface TokenResponse {
  access_token: string;
  token_type: string;
  expires_in: number;
  user_id: string;
  roles: string[];
}
export interface Me {
  user_id?: string;
  id?: string;
  email: string;
  roles?: string[];
  role?: string;
  display_name: string;
}
export interface StudentLink {
  id: string;
  teacher_id: string;
  student_id: string;
  display_name: string;
  subject?: string;
  goal?: string;
  hourly_rate?: number;
  status: string;
}
export interface Lesson {
  id: string;
  teacher_id: string;
  student_id: string;
  starts_at: string;
  ends_at: string;
  status: string;
  topic?: string;
  price?: number;
  file_ids?: string[];
}
export interface CompleteLessonResponse {
  lesson: Lesson;
  charge_status: "pending" | "created" | string;
}
export interface Assignment {
  id: string;
  teacher_id: string;
  student_id: string;
  title: string;
  description?: string;
  status: string;
  created_at?: string;
  due_at?: string | null;
  file_ids?: string[];
}
export interface Submission {
  id: string;
  assignment_id: string;
  student_id: string;
  text_answer?: string;
  status: string;
  submitted_at?: string;
  file_ids?: string[];
}
export interface Comment {
  id: string;
  assignment_id: string;
  author_id: string;
  text: string;
  created_at?: string;
}
export interface AssignmentDetail extends Assignment {
  submissions?: Submission[];
  comments?: Comment[];
  file_ids?: string[];
}
export interface Receipt {
  id: string;
  teacher_id: string;
  student_id: string;
  file_id: string;
  amount: number;
  currency?: string;
  status: string;
  submitted_at?: string;
  reviewed_at?: string | null;
  comment?: string | null;
}
export interface Balance {
  student_id: string;
  currency: string;
  balance: number;
}
export interface FinanceSummary {
  balance_amount: number;
  debt_amount: number;
  overpaid_amount: number;
  currency: string;
  pending_receipts_count: number;
  pending_receipts_amount: number;
  last_payment_at?: string;
  updated_at?: string;
}
export interface ActivitySummary {
  upcoming_lessons_count: number;
  completed_lessons_count: number;
  cancelled_lessons_count: number;
  active_assignments_count: number;
  submitted_assignments_count: number;
  reviewed_assignments_count: number;
  last_lesson_at?: string;
  next_lesson_at?: string;
  updated_at?: string;
}
export interface StudentSummary {
  teacher_id: string;
  teacher_name?: string;
  student_id: string;
  student_name?: string;
  finance: FinanceSummary;
  activity: ActivitySummary;
  updated_at?: string;
}
export interface TeacherDashboard {
  teacher_id: string;
  students_count: number;
  upcoming_lessons_count: number;
  pending_submissions_count: number;
  pending_receipts_count: number;
  pending_receipts_amount: number;
  total_debt_amount: number;
  total_overpaid_amount: number;
  students_with_debt_count: number;
  students: StudentSummary[];
  updated_at?: string;
}
export interface StudentDashboard {
  student_id: string;
  total_debt_amount: number;
  total_overpaid_amount: number;
  pending_receipts_count: number;
  pending_receipts_amount: number;
  summaries: StudentSummary[];
  updated_at?: string;
}
export interface Transaction {
  id: string;
  teacher_id: string;
  student_id: string;
  type: "charge" | "payment" | "correction" | "refund" | string;
  amount: number;
  currency: string;
  lesson_id?: string | null;
  receipt_id?: string | null;
  comment?: string | null;
  created_at?: string;
}
export interface FileMeta {
  id: string;
  owner_user_id: string;
  purpose: string;
  original_name?: string;
}
export interface AppNotification {
  id: string;
  user_id: string;
  type: string;
  title: string;
  body: string;
  payload?: Record<string, unknown>;
  is_read: boolean;
  created_at?: string;
}
export interface ChatMessage {
  id: string;
  dialog_id: string;
  sender_id: string;
  text: string;
  file_ids?: string[];
  created_at?: string;
}
export interface ChatDialog {
  id: string;
  teacher_id: string;
  student_id: string;
  created_at?: string;
  last_message_at?: string | null;
  unread_count: number;
  last_message?: ChatMessage | null;
}
export interface ChatReadMarker {
  dialog_id: string;
  user_id: string;
  last_read_message_id: string;
  last_read_at: string;
}
