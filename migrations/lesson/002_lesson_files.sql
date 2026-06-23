-- lesson-service: materials attached to lessons.
-- Files live in file-service; lesson-service stores only file_id.
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

CREATE TABLE IF NOT EXISTS lesson_files (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    lesson_id  UUID NOT NULL REFERENCES lessons (id) ON DELETE CASCADE,
    file_id    UUID NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (lesson_id, file_id)
);

CREATE INDEX IF NOT EXISTS idx_lesson_files_lesson
    ON lesson_files (lesson_id);

COMMIT;
