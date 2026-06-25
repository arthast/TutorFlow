-- finance / finance_db — 5L.4: идемпотентность компенсации отменённого занятия.
-- Owner: Agent (5L). На одно занятие — не более одной компенсирующей correction.
-- Manual-коррекции (Phase D) имеют lesson_id = NULL и под индекс не попадают
-- (NULL в unique-индексе не конфликтует), поэтому их можно сколько угодно.
-- Идемпотентно (IF NOT EXISTS) — повторный прогон migrator/migrate.sh безопасен.
CREATE UNIQUE INDEX IF NOT EXISTS uq_correction_lesson
    ON financial_transactions (lesson_id)
    WHERE type = 'correction';
