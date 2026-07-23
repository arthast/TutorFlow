-- file-service / file_db — первичная схема
-- В MVP бинарь лежит в локальном томе (FILE_STORAGE_DIR); в БД — метаданные.
BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

-- purpose: assignment_attachment | submission_file | payment_receipt
-- (chat_message — позже).
CREATE TABLE IF NOT EXISTS files (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_user_id UUID NOT NULL,
    purpose       TEXT NOT NULL
                  CHECK (purpose IN ('assignment_attachment', 'submission_file',
                                     'payment_receipt')),
    original_name TEXT NOT NULL,
    content_type  TEXT NOT NULL,
    size_bytes    BIGINT NOT NULL,
    storage_key   TEXT NOT NULL UNIQUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_files_owner ON files (owner_user_id);

COMMIT;
