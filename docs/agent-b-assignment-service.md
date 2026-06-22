# Agent B notes: assignment-service

## Scope

Service owner: Agent B.

Implemented branch: `feat/assignment-internal`.

Contract source: `docs/api-contracts/assignment.openapi.yaml`.

Do not touch: identity-service, file-service, api-gateway. File ids are stored
as opaque `file_id` values; file-service is not called by assignment-service.
Identity access check is behind a local client interface and currently stubbed.

## Implemented

- `/health` returns common health response.
- Internal assignment endpoints:
  - `POST /internal/assignments`
  - `GET /internal/assignments`
  - `GET /internal/assignments/{assignmentId}`
  - `POST /internal/assignments/{assignmentId}/submit`
  - `POST /internal/assignments/{assignmentId}/review`
  - `POST /internal/assignments/{assignmentId}/comments`
- PostgreSQL repository, domain layer, thin HTTP handlers.
- Identity access check is isolated behind `IdentityClient` and currently stubbed.

## Domain decisions

- Creating an assignment requires teacher role and checks teacher-student access.
- Listing assignments returns own assignments by role:
  - teacher: `teacher_id = X-User-Id`;
  - student: `student_id = X-User-Id`.
- Assignment detail follows the contract and does not require auth headers.
- Submission requires student role and assignment ownership.
- Submission requires either `text_answer` or at least one `file_id`.
- Review requires teacher role and assignment ownership.
- `review` path has no `submissionId` in the contract, so it reviews the latest
  submission for the assignment.
- Review status mapping:
  - `reviewed` -> assignment status `reviewed`;
  - `needs_fix` -> assignment status `needs_fix`;
  - `accepted` -> assignment status `done`.
- Review `comment`, when present, is stored as an assignment comment authored by
  the assignment teacher.
- Comments can be created only by the assignment teacher or student.

## Verification

Commands run:

```bash
docker compose build assignment-service
./scripts/migrate.sh assignment
docker compose up -d postgres assignment-service
docker compose exec assignment-service curl -s -i http://localhost:8083/health
```

Smoke path run with test ids:

- teacher: `33333333-4444-4555-8666-777777777777`
- student: `44444444-5555-4666-8777-888888888888`
- assignment file: `55555555-6666-4777-8888-999999999999`
- submission file: `66666666-7777-4888-8999-aaaaaaaaaaaa`

Checked:

- teacher creates assignment with `file_ids`;
- teacher list contains created assignment;
- student list contains created assignment;
- student submits text answer and `file_ids`;
- teacher reviews latest submission with `accepted`;
- assignment detail status becomes `done`;
- review comment and student comment are present in detail;
- detail includes assignment file id and submission file id;
- missing `title` returns `400 validation_error` envelope.

Latest smoke ids:

- assignment: `3d121169-e0a6-4cfb-89f6-ddae570e019f`
- submission: `4bd871ac-4bee-42b9-9995-4d0f315798c3`

## Current contract notes

No assignment contract changes were made.

## Next work

1. Commit this assignment-service slice after review.
2. Open PR and let Lead merge to `main`.
3. Replace `IdentityClient` stub with real identity
   `POST /internal/relations/check-access` when Agent A exposes it.
