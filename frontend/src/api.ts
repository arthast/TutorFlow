// Тонкий клиент к api-gateway. Единственная точка входа; во внутренние сервисы не ходим.

const BASE_URL: string =
  (import.meta.env.VITE_API_URL as string | undefined) ?? "http://localhost:8080";

const TOKEN_KEY = "tutorflow_token";

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
  constructor(status: number, code: string, message: string) {
    super(message);
    this.code = code;
    this.status = status;
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

  const res = await fetch(BASE_URL + path, { method, headers, body });
  const data = await parse(res);

  if (!res.ok) {
    const env = data as { error?: { code?: string; message?: string } };
    const code = env?.error?.code ?? "error";
    const message = env?.error?.message ?? `Ошибка ${res.status}`;
    throw new ApiError(res.status, code, message);
  }
  return data as T;
}

async function requestBlob(path: string): Promise<Blob> {
  const token = getToken();
  const headers: Record<string, string> = {};
  if (token) headers["Authorization"] = "Bearer " + token;
  const res = await fetch(BASE_URL + path, { headers });
  if (!res.ok) {
    const data = await parse(res);
    const env = data as { error?: { code?: string; message?: string } };
    throw new ApiError(res.status, env?.error?.code ?? "error", env?.error?.message ?? `Ошибка ${res.status}`);
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
}
export interface Balance {
  student_id: string;
  currency: string;
  balance: number;
}
export interface FileMeta {
  id: string;
  owner_user_id: string;
  purpose: string;
  original_name?: string;
}
