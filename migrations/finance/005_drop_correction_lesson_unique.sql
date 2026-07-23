-- finance-service / finance_db
-- Correction idempotency for lesson.cancelled/lesson.restored moved to the
-- atomic processed_events(event_id) inbox. A single lesson may now have both
-- correction(-price) and correction(+price), while charge stays unique.
DROP INDEX IF EXISTS uq_correction_lesson;
