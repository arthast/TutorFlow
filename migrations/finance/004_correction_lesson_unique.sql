-- finance / finance_db — 5L.4: идемпотентность компенсации отменённого занятия.
-- Owner: Agent (5L). На одно занятие — не более одной компенсирующей correction.
-- Manual-коррекции (Phase D) имеют lesson_id = NULL и под индекс не попадают
-- (NULL в unique-индексе не конфликтует), поэтому их можно сколько угодно.
-- 5L follow-up later removed this index (see 005_drop_correction_lesson_unique).
-- Keep this historical migration replay-safe for dev databases that already
-- contain both cancel(-price) and restore(+price) corrections for one lesson.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
    FROM financial_transactions
    WHERE type = 'correction' AND lesson_id IS NOT NULL
    GROUP BY lesson_id
    HAVING COUNT(*) > 1
  ) THEN
    CREATE UNIQUE INDEX IF NOT EXISTS uq_correction_lesson
        ON financial_transactions (lesson_id)
        WHERE type = 'correction';
  END IF;
END
$$;
