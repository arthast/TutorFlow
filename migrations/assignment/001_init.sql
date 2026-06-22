-- assignment-service / assignment_db — первичная схема (PLAN §8.3).
-- Owner: Agent B. Идемпотентно (IF NOT EXISTS).
-- Файлы живут в file-service; здесь хранится только file_id (без FK на чужую БД).
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

-- Статусы ДЗ: assigned | submitted | reviewed | needs_fix | done | expired.
CREATE TABLE IF NOT EXISTS assignments (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    teacher_id  UUID NOT NULL,
    student_id  UUID NOT NULL,
    title       TEXT NOT NULL,
    description TEXT,
    due_at      TIMESTAMPTZ,
    status      TEXT NOT NULL DEFAULT 'assigned'
                CHECK (status IN ('assigned', 'submitted', 'reviewed',
                                  'needs_fix', 'done', 'expired')),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS assignment_files (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    assignment_id UUID NOT NULL REFERENCES assignments (id) ON DELETE CASCADE,
    file_id       UUID NOT NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (assignment_id, file_id)
);

-- Статусы решения: submitted | reviewed | needs_fix | accepted.
CREATE TABLE IF NOT EXISTS submissions (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    assignment_id UUID NOT NULL REFERENCES assignments (id) ON DELETE CASCADE,
    student_id    UUID NOT NULL,
    text_answer   TEXT,
    status        TEXT NOT NULL DEFAULT 'submitted'
                  CHECK (status IN ('submitted', 'reviewed', 'needs_fix',
                                    'accepted')),
    submitted_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS submission_files (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    submission_id UUID NOT NULL REFERENCES submissions (id) ON DELETE CASCADE,
    file_id       UUID NOT NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (submission_id, file_id)
);

-- Комментарии живут здесь же (НЕ chat-service) (PLAN §8.3).
CREATE TABLE IF NOT EXISTS assignment_comments (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    assignment_id UUID NOT NULL REFERENCES assignments (id) ON DELETE CASCADE,
    author_id     UUID NOT NULL,
    text          TEXT NOT NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_assignments_teacher ON assignments (teacher_id);
CREATE INDEX IF NOT EXISTS idx_assignments_student ON assignments (student_id);
CREATE INDEX IF NOT EXISTS idx_submissions_assignment
    ON submissions (assignment_id);
CREATE INDEX IF NOT EXISTS idx_comments_assignment
    ON assignment_comments (assignment_id);

COMMIT;
