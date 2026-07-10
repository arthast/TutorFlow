-- chat-service / chat_db_shardN — личная переписка teacher<->student.
-- Идемпотентно (IF NOT EXISTS). Один диалог на пару (UNIQUE).
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

-- Диалог строго между связанными teacher и student (пара проверяется в сервисе
-- через identity CheckTeacherStudentAccess). UNIQUE(teacher_id, student_id)
-- локально защищает пару от дубля на одном шарде. Глобальную уникальность
-- пары между шардами обеспечивает детерминированный UUIDv5 dialogs.id.
CREATE TABLE IF NOT EXISTS dialogs (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    teacher_id      UUID NOT NULL,
    student_id      UUID NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_message_at TIMESTAMPTZ,
    UNIQUE (teacher_id, student_id)
);

CREATE TABLE IF NOT EXISTS messages (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    dialog_id  UUID NOT NULL REFERENCES dialogs(id) ON DELETE CASCADE,
    sender_id  UUID NOT NULL,
    text       TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Пагинация/выборка сообщений диалога по времени.
CREATE INDEX IF NOT EXISTS idx_chat_messages_dialog
    ON messages (dialog_id, created_at, id);

CREATE TABLE IF NOT EXISTS message_attachments (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    message_id UUID NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    file_id    UUID NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_chat_attachments_message
    ON message_attachments (message_id);

-- Указатель «прочитано до» на участника диалога. Двигается только вперёд.
CREATE TABLE IF NOT EXISTS read_markers (
    dialog_id            UUID NOT NULL REFERENCES dialogs(id) ON DELETE CASCADE,
    user_id              UUID NOT NULL,
    last_read_message_id UUID NOT NULL,
    last_read_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at           TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (dialog_id, user_id)
);

-- Transactional outbox (как в lesson/finance). chat в v1 только producer.
CREATE TABLE IF NOT EXISTS outbox_events (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    aggregate_type TEXT NOT NULL,
    aggregate_id   UUID NOT NULL,
    event_type     TEXT NOT NULL,
    event_version  INT  NOT NULL DEFAULT 1,
    payload        JSONB NOT NULL,
    status         TEXT NOT NULL DEFAULT 'pending'
                   CHECK (status IN ('pending', 'published')),
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    published_at   TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_chat_outbox_pending
    ON outbox_events (created_at)
    WHERE status = 'pending';

COMMIT;
