# File-service: symmetric download access

Stage: 3.6.0. Changed **file-service only**. Contract `file.openapi.yaml` already
includes `lesson_material` in the `purpose` enum (updated by coordinator).

## Problem

`FileService::Download` only allowed:
- the owner; or
- a teacher whose link to the file owner (treated as student) is active
  (`check-access(requester_teacher, owner_student)`).

So a **student could not download a file uploaded by their teacher** (assignment
attachment, lesson material) — the relation arrow was wrong-way → `403`.

## Change

1. `FileService::Download` — the non-owner branch is now symmetric:
   - if the requester is a teacher → `check-access(requester, owner)`
     (requester as teacher, owner as student) — unchanged;
   - else (requester is a student) → `check-access(owner, requester)`
     (owner as teacher, requester as student) — **new reverse branch**.
   - if `allowed` → serve the file; otherwise `403`.
   The owner branch is preserved.
2. Upload purpose whitelist in `ParseUploadRequest` gained `lesson_material`
   (`services/file-service/src/handlers/file_handlers.cpp`).
3. DB CHECK constraint `files_purpose_check` did not know `lesson_material`, so
   uploads with that purpose failed with `500`. Added migration
   `migrations/file/002_lesson_material_purpose.sql` (idempotent: DROP IF EXISTS
   + recreate constraint with the 4th value).

Files:
- `services/file-service/src/domain/file_service.cpp`
- `services/file-service/src/handlers/file_handlers.cpp`
- `migrations/file/002_lesson_material_purpose.sql` (new)
- `tests/test_access.py` (new `test_file_symmetric_download_access`)

## Validation

- `docker compose build file-service` -> OK; `up -d file-service`; `/health` -> 200.
- `./scripts/migrate.sh file` -> applies 002 cleanly (idempotent).
- Download matrix (via gateway `/files/{id}/download`):
  - owner (teacher) downloads own `lesson_material` -> 200;
  - teacher downloads own student's `submission_file` -> 200;
  - linked student downloads teacher's `lesson_material` -> 200;
  - linked student downloads teacher's `assignment_attachment` -> 200;
  - student downloads a foreign teacher's file -> 403;
  - foreign student downloads the file -> 403.
- `upload purpose=lesson_material` -> 201.
- `python3 scripts/smoke_mvp.py` -> `SMOKE OK`.
- `python3 -m pytest tests/ -q` -> 15 passed.

## Note for coordinator

The `lesson_material` value required a DB migration in addition to the handler
whitelist. On a fresh `docker compose up`, run `./scripts/migrate.sh file` (or
`./scripts/migrate.sh all`) so `files_purpose_check` includes the new value.
