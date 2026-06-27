-- file-service / file_db — добавить purpose='chat_message' (roadmap 5J).
-- chat-service хранит file_id вложений; доступ на скачивание уже покрыт
-- симметричным teacher<->student доступом file-service. Идемпотентно.
BEGIN;

ALTER TABLE files DROP CONSTRAINT IF EXISTS files_purpose_check;
ALTER TABLE files ADD CONSTRAINT files_purpose_check
    CHECK (purpose IN ('assignment_attachment', 'submission_file',
                       'payment_receipt', 'lesson_material', 'chat_message'));

COMMIT;
