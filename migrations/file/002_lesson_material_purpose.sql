-- file-service / file_db — добавить purpose='lesson_material'
-- Идемпотентно (DROP IF EXISTS + пересоздание constraint).
-- Контракт file.openapi.yaml уже включает lesson_material в enum purpose.
BEGIN;

ALTER TABLE files DROP CONSTRAINT IF EXISTS files_purpose_check;
ALTER TABLE files ADD CONSTRAINT files_purpose_check
    CHECK (purpose IN ('assignment_attachment', 'submission_file',
                       'payment_receipt', 'lesson_material', 'chat_message'));

COMMIT;
