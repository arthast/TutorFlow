-- lesson-service / lesson_db — первичная схема
-- Идемпотентно (IF NOT EXISTS).
-- Границы БД: teacher_id/student_id живут в identity_db,
-- поэтому здесь они БЕЗ внешних ключей — только колонки + индексы.
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

-- Слоты доступности. Статусы: open | booked
CREATE TABLE IF NOT EXISTS availability_slots (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    teacher_id UUID NOT NULL,
    starts_at  TIMESTAMPTZ NOT NULL,
    ends_at    TIMESTAMPTZ NOT NULL,
    status     TEXT NOT NULL DEFAULT 'open'
               CHECK (status IN ('open', 'booked')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Занятия. Статусы: scheduled | completed | cancelled
-- price — снимок цены, фиксируется при создании занятия.
CREATE TABLE IF NOT EXISTS lessons (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    teacher_id   UUID NOT NULL,
    student_id   UUID NOT NULL,
    slot_id      UUID REFERENCES availability_slots (id) ON DELETE SET NULL,
    starts_at    TIMESTAMPTZ NOT NULL,
    ends_at      TIMESTAMPTZ NOT NULL,
    status       TEXT NOT NULL DEFAULT 'scheduled'
                 CHECK (status IN ('scheduled', 'completed', 'cancelled')),
    topic        TEXT,
    notes        TEXT,
    price        NUMERIC(12, 2) NOT NULL,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    completed_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_slots_teacher  ON availability_slots (teacher_id);
CREATE INDEX IF NOT EXISTS idx_lessons_teacher ON lessons (teacher_id);
CREATE INDEX IF NOT EXISTS idx_lessons_student ON lessons (student_id);

COMMIT;
