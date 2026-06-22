# Agent A — file-service implementation notes

Branch: `feat/A-file`  
Commit: `0b37171`  
Status: **DONE — ready for PR**

---

## What was implemented

### Endpoints

| Method | Path | Handler |
|--------|------|---------|
| POST | `/internal/files` | `UploadHandler` |
| GET | `/internal/files/{fileId}` | `GetMetaHandler` |
| GET | `/internal/files/{fileId}/download` | `DownloadHandler` |

### Upload (multipart/form-data)
- Content-Type must be `multipart/form-data` with boundary.
- Required fields: `file` (binary part with filename), `purpose` (one of `assignment_attachment`, `submission_file`, `payment_receipt`).
- Optional: `resource_id` (accepted in multipart but ignored in MVP — not stored).
- Size limit: `max-size-bytes` from config (default 10 MB).
- Storage key: UUID generated via `userver::utils::generators::GenerateUuid()`.
- File written to `FILE_STORAGE_DIR/<uuid>` using `AsyncNoSpan` on `fs-task-processor`.

### Download access check
- Owner (`owner_user_id == X-User-Id`): always allowed.
- Teacher (X-User-Roles contains "teacher"): calls `POST /internal/relations/check-access`
  with `{teacher_id: requester, student_id: file.owner_user_id}`. Allowed if `allowed=true`.
- Otherwise: 403.

### DB component name: `file-db` (env: `FILE_DATABASE_URL`)

### Component names
- `file-repository`
- `file-domain-service` ← `storage-dir#env: FILE_STORAGE_DIR`, `max-size-bytes: 10485760`
- `identity-client` ← `base-url#env: IDENTITY_SERVICE_URL`
- `file-upload-handler`, `file-get-meta-handler`, `file-download-handler`

### File I/O
- Uses `std::fstream` inside `userver::engine::AsyncNoSpan(fs_tp_, ...)` to avoid blocking main-task-processor.
- `fs_tp_` is the `fs-task-processor` obtained from `context.GetTaskProcessor`.

---

## Known limitations (MVP)
- `resource_id` field from upload request is not stored (not in DB schema).
- Download response streams entire file into memory before sending (fine for 10 MB limit).
- No file type validation (any content-type is accepted).

---

## Next tasks (Lead roadmap)
5. **api-gateway** — JWT validate locally, set X-User-*, route to internal services
