import { createContext, useContext, useEffect, useState, type ReactNode } from "react";
import { api, getToken, setToken, clearToken, type Me, type TokenResponse } from "./api";

interface AuthState {
  user: Me | null;
  loading: boolean;
  login: (email: string, password: string) => Promise<void>;
  register: (data: RegisterData) => Promise<void>;
  logout: () => void;
  role: "teacher" | "student" | null;
}

export interface RegisterData {
  email: string;
  password: string;
  role: "teacher" | "student";
  display_name: string;
}

const AuthContext = createContext<AuthState | null>(null);

function normalizeMe(me: Me): Me {
  const role = me.role ?? me.roles?.[0];
  return {
    ...me,
    user_id: me.user_id ?? me.id,
    roles: me.roles ?? (role ? [role] : []),
  };
}

function resolveRole(user: Me | null): "teacher" | "student" | null {
  if (!user) return null;
  const roles = user.roles ?? (user.role ? [user.role] : []);
  if (roles.includes("teacher")) return "teacher";
  if (roles.includes("student")) return "student";
  return null;
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<Me | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    if (!getToken()) {
      setLoading(false);
      return;
    }
    api
      .get<Me>("/me")
      .then((me) => setUser(normalizeMe(me)))
      .catch(() => clearToken())
      .finally(() => setLoading(false));
  }, []);

  async function applyToken(tok: TokenResponse) {
    setToken(tok.access_token);
    const me = await api.get<Me>("/me");
    setUser(normalizeMe(me));
  }

  async function login(email: string, password: string) {
    const tok = await api.postPublic<TokenResponse>("/auth/login", { email, password });
    await applyToken(tok);
  }

  async function register(data: RegisterData) {
    const tok = await api.postPublic<TokenResponse>("/auth/register", data);
    await applyToken(tok);
  }

  function logout() {
    clearToken();
    setUser(null);
  }

  const role = resolveRole(user);

  return (
    <AuthContext.Provider value={{ user, loading, login, register, logout, role }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error("useAuth must be used within AuthProvider");
  return ctx;
}
