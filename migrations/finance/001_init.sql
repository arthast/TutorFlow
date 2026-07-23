-- finance-service / finance_db — первичная схема
-- Идемпотентно (IF NOT EXISTS).
-- Модель: append-only журнал операций + чеки. Операции не редактируем.
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

-- Тип операции: charge | payment | correction | refund
-- balance = sum(charge) - sum(payment) + sum(correction) - sum(refund).
CREATE TABLE IF NOT EXISTS financial_transactions (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    teacher_id UUID NOT NULL,
    student_id UUID NOT NULL,
    type       TEXT NOT NULL
               CHECK (type IN ('charge', 'payment', 'correction', 'refund')),
    amount     NUMERIC(12, 2) NOT NULL,
    currency   TEXT NOT NULL DEFAULT 'RUB',
    lesson_id  UUID,
    receipt_id UUID,
    comment    TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Идемпотентность charge: один charge на занятие
-- Частичный уникальный индекс — только для type='charge'.
CREATE UNIQUE INDEX IF NOT EXISTS uq_charge_lesson
    ON financial_transactions (lesson_id)
    WHERE type = 'charge';

-- Идемпотентность confirm receipt: один payment на чек.
CREATE UNIQUE INDEX IF NOT EXISTS uq_payment_receipt
    ON financial_transactions (receipt_id)
    WHERE type = 'payment';

CREATE INDEX IF NOT EXISTS idx_ft_student ON financial_transactions (student_id);
CREATE INDEX IF NOT EXISTS idx_ft_teacher ON financial_transactions (teacher_id);

-- Чеки оплаты. Статус: pending_review | confirmed | rejected
-- Загрузка чека баланс НЕ меняет; payment создаётся только при confirm.
CREATE TABLE IF NOT EXISTS payment_receipts (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    teacher_id   UUID NOT NULL,
    student_id   UUID NOT NULL,
    file_id      UUID NOT NULL,
    amount       NUMERIC(12, 2) NOT NULL,
    currency     TEXT NOT NULL DEFAULT 'RUB',
    status       TEXT NOT NULL DEFAULT 'pending_review'
                 CHECK (status IN ('pending_review', 'confirmed', 'rejected')),
    submitted_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    reviewed_at  TIMESTAMPTZ,
    reviewed_by  UUID,
    comment      TEXT
);

CREATE INDEX IF NOT EXISTS idx_receipts_student ON payment_receipts (student_id);
CREATE INDEX IF NOT EXISTS idx_receipts_teacher ON payment_receipts (teacher_id);
CREATE INDEX IF NOT EXISTS idx_receipts_status  ON payment_receipts (status);

COMMIT;
