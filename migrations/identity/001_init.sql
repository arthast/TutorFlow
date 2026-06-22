-- identity-service / identity_db — первичная схема (PLAN §8.1).
-- Owner: Agent A. Идемпотентно (IF NOT EXISTS).
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

CREATE TABLE IF NOT EXISTS users (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    email         TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    role          TEXT NOT NULL CHECK (role IN ('teacher', 'student')),
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS teacher_profiles (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id      UUID NOT NULL UNIQUE REFERENCES users (id) ON DELETE CASCADE,
    display_name TEXT NOT NULL,
    timezone     TEXT,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS student_profiles (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id      UUID NOT NULL UNIQUE REFERENCES users (id) ON DELETE CASCADE,
    display_name TEXT NOT NULL,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Связь teacher<->student. Статусы: invited | active | archived (PLAN §8.1).
CREATE TABLE IF NOT EXISTS teacher_student_links (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    teacher_id  UUID NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    student_id  UUID NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    subject     TEXT,
    goal        TEXT,
    hourly_rate NUMERIC(12, 2),
    status      TEXT NOT NULL DEFAULT 'invited'
                CHECK (status IN ('invited', 'active', 'archived')),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (teacher_id, student_id)
);

CREATE INDEX IF NOT EXISTS idx_tsl_teacher ON teacher_student_links (teacher_id);
CREATE INDEX IF NOT EXISTS idx_tsl_student ON teacher_student_links (student_id);

COMMIT;
