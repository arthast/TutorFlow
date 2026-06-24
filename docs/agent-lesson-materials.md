# Lesson materials

Stage: 3.6.3.

Changes:
- Added `migrations/lesson/002_lesson_files.sql`.
- `lesson-service` stores lesson material ids in `lesson_files`.
- `CreateLessonRequest.file_ids` is optional; missing/null means no materials.
- Lesson responses include `file_ids` for create, list, get, complete, and cancel.
- `file_id` validation is intentionally not performed here; files live in
  file-service and lesson-service stores only ids, matching assignment-service.

Validation:
- `docker compose build lesson-service` -> OK.
- `./scripts/migrate.sh lesson` -> OK.
- POST `/lessons` with `file_ids` -> 201 and response includes the ids.
- GET `/lessons/{id}` -> includes the same `file_ids`.
- POST `/lessons` without `file_ids` -> 201 and `file_ids: []`.
- `python3 scripts/smoke_mvp.py` -> `SMOKE OK`.
- `python3 -m pytest tests/ -v` -> 15 passed.
