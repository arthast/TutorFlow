-- finance-service / finance_db — consumer inbox/idempotency
-- Kafka delivery is at-least-once; consumers record processed event_id.
BEGIN;

CREATE TABLE IF NOT EXISTS processed_events (
    event_id     UUID PRIMARY KEY,
    event_type   TEXT NOT NULL,
    processed_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_processed_events_type
    ON processed_events (event_type, processed_at);

COMMIT;
