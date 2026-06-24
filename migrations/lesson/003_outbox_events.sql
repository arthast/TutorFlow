-- lesson-service / lesson_db — transactional outbox (Этап 5E-1).
-- Owner: Agent B. Идемпотентно (IF NOT EXISTS).
-- Outbox: в одной транзакции с complete-занятия пишется строка события;
-- отдельный publisher шлёт её в Kafka и помечает published (at-least-once).
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

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

-- Поллинг publisher'ом: только pending, в порядке создания.
CREATE INDEX IF NOT EXISTS idx_outbox_pending
    ON outbox_events (created_at)
    WHERE status = 'pending';

COMMIT;
