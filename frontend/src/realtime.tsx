import {
  createContext,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from "react";
import { getToken } from "./api";
import { useAuth } from "./auth";

const WS_URL: string =
  (import.meta.env.VITE_REALTIME_URL as string | undefined) ?? "ws://localhost:8089/ws";

export interface RealtimeEvent<T = Record<string, unknown>> {
  type: string;
  payload: T;
  seq: number;
}

interface Toast {
  id: string;
  title: string;
  body: string;
}

interface RealtimeState {
  lastEvent: RealtimeEvent | null;
  online: Record<string, boolean>;
}

const RealtimeContext = createContext<RealtimeState | null>(null);

export function RealtimeProvider({ children }: { children: ReactNode }) {
  const { user } = useAuth();
  const [lastEvent, setLastEvent] = useState<RealtimeEvent | null>(null);
  const [online, setOnline] = useState<Record<string, boolean>>({});
  const [toasts, setToasts] = useState<Toast[]>([]);
  const seqRef = useRef(0);

  useEffect(() => {
    const token = getToken();
    if (!token || !user) return;
    const tokenValue = token;

    let stopped = false;
    let socket: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let pingTimer: number | null = null;
    let attempt = 0;

    function scheduleReconnect() {
      if (stopped) return;
      const delay = Math.min(1000 * 2 ** attempt, 10000);
      attempt += 1;
      reconnectTimer = window.setTimeout(connect, delay);
    }

    function connect() {
      const separator = WS_URL.includes("?") ? "&" : "?";
      socket = new WebSocket(`${WS_URL}${separator}token=${encodeURIComponent(tokenValue)}`);

      socket.onopen = () => {
        attempt = 0;
        pingTimer = window.setInterval(() => {
          if (socket?.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify({ type: "ping" }));
          }
        }, 15000);
      };

      socket.onmessage = (message) => {
        try {
          const parsed = JSON.parse(message.data) as { type?: string; payload?: Record<string, unknown> };
          if (!parsed.type) return;
          const event = {
            type: parsed.type,
            payload: parsed.payload ?? {},
            seq: ++seqRef.current,
          };
          setLastEvent(event);
          if (event.type === "presence") {
            const userId = String(event.payload.user_id ?? "");
            if (userId) {
              setOnline((prev) => ({ ...prev, [userId]: Boolean(event.payload.online) }));
            }
          }
          if (event.type === "notification") {
            const id = String(event.payload.notification_id ?? Date.now());
            setToasts((prev) => [
              {
                id,
                title: String(event.payload.title ?? "Уведомление"),
                body: String(event.payload.body ?? ""),
              },
              ...prev,
            ].slice(0, 3));
            window.setTimeout(() => {
              setToasts((prev) => prev.filter((toast) => toast.id !== id));
            }, 6000);
          }
        } catch {
          /* ignore malformed realtime frames */
        }
      };

      socket.onclose = () => {
        if (pingTimer) window.clearInterval(pingTimer);
        pingTimer = null;
        scheduleReconnect();
      };
      socket.onerror = () => socket?.close();
    }

    connect();
    return () => {
      stopped = true;
      if (reconnectTimer) window.clearTimeout(reconnectTimer);
      if (pingTimer) window.clearInterval(pingTimer);
      socket?.close();
    };
  }, [user?.user_id]);

  const value = useMemo(() => ({ lastEvent, online }), [lastEvent, online]);

  return (
    <RealtimeContext.Provider value={value}>
      {children}
      {toasts.length > 0 && (
        <div className="toast-stack">
          {toasts.map((toast) => (
            <div className="toast" key={toast.id}>
              <div className="toast-title">{toast.title}</div>
              <div className="toast-body">{toast.body}</div>
            </div>
          ))}
        </div>
      )}
    </RealtimeContext.Provider>
  );
}

export function useRealtimeEvent(
  handler: (event: RealtimeEvent) => void,
  deps: unknown[] = [],
) {
  const ctx = useContext(RealtimeContext);
  const lastSeq = useRef(0);

  useEffect(() => {
    if (!ctx?.lastEvent || ctx.lastEvent.seq === lastSeq.current) return;
    lastSeq.current = ctx.lastEvent.seq;
    handler(ctx.lastEvent);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ctx?.lastEvent, ...deps]);
}

export function useOnlineStatus(userId?: string): boolean {
  const ctx = useContext(RealtimeContext);
  return Boolean(userId && ctx?.online[userId]);
}
