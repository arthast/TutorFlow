-- lesson-service / lesson_db — запрет пересекающихся занятий преподавателя
-- (correctness-фикс). БД-гарант против double-booking и гонки двух конкурентных
-- create/reschedule: атомарность даёт сам constraint, code-level проверки
-- недостаточно.
-- Owner: Agent B. Идемпотентно: ADD CONSTRAINT не поддерживает IF NOT EXISTS,
-- поэтому наличие проверяем вручную по pg_constraint в DO-блоке.
BEGIN;

-- EXCLUDE с равенством по teacher_id (WITH =) требует gist-операторного класса
-- для скалярного типа — его даёт contrib-модуль btree_gist (входит в
-- стандартный образ postgres).
CREATE EXTENSION IF NOT EXISTS btree_gist;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
    FROM pg_constraint
    WHERE conname = 'no_overlap_teacher'
      AND conrelid = 'lessons'::regclass
  ) THEN
    -- Диапазон tstzrange полуоткрытый [) — смежные занятия (одно кончается в
    -- 10:00, следующее с 10:00) НЕ пересекаются. Гард только для
    -- status = 'scheduled': cancelled/completed время не держат, поэтому отмена
    -- освобождает слот (согласовано с поведением availability_slots).
    -- Ограничиваем пересечение только по teacher (по student — не требуется).
    ALTER TABLE lessons
      ADD CONSTRAINT no_overlap_teacher
      EXCLUDE USING gist (
        teacher_id WITH =,
        tstzrange(starts_at, ends_at) WITH &&
      )
      WHERE (status = 'scheduled');
  END IF;
END
$$;

COMMIT;
