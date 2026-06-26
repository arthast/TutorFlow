-- report-service / report_db — read-models from Kafka domain events (5H).
-- Источник истины остаётся в доменных сервисах; эти таблицы пересобираемы.
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE IF NOT EXISTS student_activity_summary (
    teacher_id UUID NOT NULL,
    student_id UUID NOT NULL,
    upcoming_lessons_count INT NOT NULL DEFAULT 0,
    completed_lessons_count INT NOT NULL DEFAULT 0,
    cancelled_lessons_count INT NOT NULL DEFAULT 0,
    active_assignments_count INT NOT NULL DEFAULT 0,
    submitted_assignments_count INT NOT NULL DEFAULT 0,
    reviewed_assignments_count INT NOT NULL DEFAULT 0,
    last_lesson_at TIMESTAMPTZ,
    next_lesson_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (teacher_id, student_id)
);

CREATE TABLE IF NOT EXISTS student_finance_summary (
    teacher_id UUID NOT NULL,
    student_id UUID NOT NULL,
    balance_amount NUMERIC(12, 2) NOT NULL DEFAULT 0,
    debt_amount NUMERIC(12, 2) NOT NULL DEFAULT 0,
    overpaid_amount NUMERIC(12, 2) NOT NULL DEFAULT 0,
    currency TEXT NOT NULL DEFAULT 'RUB',
    pending_receipts_count INT NOT NULL DEFAULT 0,
    pending_receipts_amount NUMERIC(12, 2) NOT NULL DEFAULT 0,
    last_payment_at TIMESTAMPTZ,
    last_balance_event_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (teacher_id, student_id)
);

CREATE TABLE IF NOT EXISTS teacher_summary (
    teacher_id UUID PRIMARY KEY,
    students_count INT NOT NULL DEFAULT 0,
    upcoming_lessons_count INT NOT NULL DEFAULT 0,
    pending_submissions_count INT NOT NULL DEFAULT 0,
    pending_receipts_count INT NOT NULL DEFAULT 0,
    pending_receipts_amount NUMERIC(12, 2) NOT NULL DEFAULT 0,
    total_debt_amount NUMERIC(12, 2) NOT NULL DEFAULT 0,
    total_overpaid_amount NUMERIC(12, 2) NOT NULL DEFAULT 0,
    students_with_debt_count INT NOT NULL DEFAULT 0,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS report_processed_events (
    event_id UUID PRIMARY KEY,
    event_type TEXT NOT NULL,
    processed_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_report_processed_events_type
    ON report_processed_events (event_type, processed_at);

-- Entity state tables make counters rebuildable and make reschedule/review
-- updates independent from mutable "+1/-1" counters.
CREATE TABLE IF NOT EXISTS report_lessons (
    lesson_id UUID PRIMARY KEY,
    teacher_id UUID NOT NULL,
    student_id UUID NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('scheduled', 'completed', 'cancelled')),
    starts_at TIMESTAMPTZ,
    ends_at TIMESTAMPTZ,
    event_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_report_lessons_pair
    ON report_lessons (teacher_id, student_id);

CREATE TABLE IF NOT EXISTS report_assignments (
    assignment_id UUID PRIMARY KEY,
    teacher_id UUID NOT NULL,
    student_id UUID NOT NULL,
    status TEXT NOT NULL,
    event_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_report_assignments_pair
    ON report_assignments (teacher_id, student_id);

CREATE TABLE IF NOT EXISTS report_receipts (
    receipt_id UUID PRIMARY KEY,
    teacher_id UUID NOT NULL,
    student_id UUID NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('pending_review', 'confirmed', 'rejected')),
    amount NUMERIC(12, 2) NOT NULL,
    currency TEXT NOT NULL DEFAULT 'RUB',
    event_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_report_receipts_pair
    ON report_receipts (teacher_id, student_id);

COMMIT;
