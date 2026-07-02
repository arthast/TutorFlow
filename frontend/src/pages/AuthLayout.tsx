import type { ReactNode } from "react";
import { Link } from "react-router-dom";
import { Icon } from "../ui";

export default function AuthLayout({
  title,
  subtitle,
  children,
  footer,
  back,
}: {
  title: string;
  subtitle: string;
  children: ReactNode;
  footer: ReactNode;
  back?: { to: string; label: string };
}) {
  return (
    <div className="auth-page">
      <section className="auth-brand-panel">
        <div className="auth-brand-top">
          <div className="auth-logo">T</div>
          <span className="auth-brand-name">TutorFlow</span>
        </div>

        <div className="auth-brand-hero">
          <h1>Спокойное пространство для занятий и расчётов</h1>
          <p>Расписание, домашние задания, проверка работ и понятный учёт оплат — всё в одном рабочем кабинете.</p>
          <div className="auth-feature-list">
            <Feature icon="calendar_month" title="Расписание и занятия" text="переносы, статусы, история" />
            <Feature icon="receipt_long" title="Оплаты по чекам" text="подтверждение вручную, без карт" />
            <Feature icon="chat_bubble" title="Чат и уведомления" text="в реальном времени" />
          </div>
        </div>

        <div className="auth-copyright">© 2026 TutorFlow · Учебный рабочий инструмент</div>
      </section>

      <main className="auth-form-side">
        <div className="auth-form-card">
          {back && (
            <Link className="auth-back" to={back.to}>
              <Icon name="arrow_back" />{back.label}
            </Link>
          )}
          <div className="auth-form-heading">
            <h2>{title}</h2>
            <p>{subtitle}</p>
          </div>
          {children}
          {footer && <div className="auth-footer">{footer}</div>}
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
