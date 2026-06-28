import type { ReactNode } from "react";
import { Link } from "react-router-dom";
import { Icon } from "../ui";

export default function AuthLayout({
  title,
  subtitle,
  children,
  footer,
}: {
  title: string;
  subtitle: string;
  children: ReactNode;
  footer: ReactNode;
}) {
  return (
    <div className="auth-page">
      <section className="auth-brand-panel">
        <div className="auth-logo">T</div>
        <div>
          <h1>TutorFlow</h1>
          <p>Кабинет преподавателя и ученика</p>
        </div>
        <div className="auth-feature-list">
          <Feature icon="calendar_month" title="Расписание и занятия" text="переносы, статусы, история" />
          <Feature icon="receipt_long" title="Оплаты по чекам" text="подтверждение вручную, без карт" />
          <Feature icon="chat_bubble" title="Чат и уведомления" text="в реальном времени" />
        </div>
      </section>

      <main className="auth-form-side">
        <div className="auth-form-card">
          <div className="auth-form-heading">
            <h2>{title}</h2>
            <p>{subtitle}</p>
          </div>
          {children}
          <div className="auth-footer">{footer}</div>
        </div>
      </main>
    </div>
  );
}

function Feature({ icon, title, text }: { icon: string; title: string; text: string }) {
  return (
    <div className="auth-feature">
      <div><Icon name={icon} /></div>
      <span>
        <strong>{title}</strong>
        <em>{text}</em>
      </span>
    </div>
  );
}

export function AuthLink({ to, children }: { to: string; children: ReactNode }) {
  return <Link to={to}>{children}</Link>;
}
