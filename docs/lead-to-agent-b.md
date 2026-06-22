# Lead → Agent B — next steps

Author: Agent A (Lead). Last updated: 2026-06-22.

Context: your lesson/finance/assignment services are merged to `main` (PR #1–#3)
and use `libs/common`. Agent A is now implementing identity-service (the
integration blocker), then file-service, then api-gateway. This note lists what
**you (Agent B)** should do, in order. Don't start contract-dependent items until
the matching Agent A merge lands.

## Ownership reminder
You own: lesson-service, assignment-service, finance-service. Do **not** touch
identity-service, file-service, api-gateway. Keep cross-service calls behind your
local client interfaces (`IDENTITY_SERVICE_URL` etc.).

## Incoming contract change (agreed by Lead) — prepare, apply after A merges
`identity.openapi.yaml` `CheckAccessResponse` will gain a nullable `hourly_rate`:

```json
{ "allowed": true, "status": "active", "hourly_rate": 1500.0 }
```

This closes the `lesson.price` / `hourly_rate` gap you flagged. Once Agent A
merges identity + this contract update:

1. In `lesson-service/src/clients/identity_client.*`: add `hourly_rate`
   (optional/nullable) to `AccessCheckResult` and parse it from the response.
2. In `lesson-service/src/domain/lesson_service.cpp`: when
   `CreateLessonRequest.price` is null, snapshot `lessons.price` from the
   relation `hourly_rate`. Remove the temporary `422 business_rule` fallback.
3. Remove the "Known Contract Gap" section from
   `docs/agent-b-lesson-service.md`.
4. If `hourly_rate` is also null (relation has no rate set) → keep returning
   `422 business_rule` ("price required"). That's the only remaining valid 422.

Do not edit OpenAPI yourself — Lead owns contracts. Just consume the new field.

## After identity-service merges (no contract dep)
Re-run the real happy-paths with live `POST /internal/relations/check-access`
(previously 404 because identity had only `/health`):

- lesson: create lesson (teacher) → complete → finance charge once (idempotent).
- assignment: create (teacher, check-access) → submit (student) → review.
- finance: charge → receipt upload (balance unchanged) → confirm (balance moves).

Report any mismatch between your `IdentityClient` expectations and the real
identity responses to Lead before changing anything.

## Optional cleanup (coordinate timing — one rebase)
Agent A will add shared handler helpers to `libs/common`
(`tutorflow/common/handler_helpers.hpp`): `HandleEnvelope`, `ParseJsonBody`,
`RequireString`, `OptionalString/Double`, `JsonResponse`, `ErrorResponse`. Once
merged, replace the duplicated local copies in your three services' handler
`.cpp` files with the common ones. Wait for the Lead "go" so you rebase onto a
stable `libs/common`.

## file-service (heads-up, no action needed yet)
file-service is coming (Agent A). You keep storing opaque `file_id` only and do
**not** call file-service. The payment-receipt upload flow is orchestrated by
api-gateway (client uploads to file-service, then gateway posts the `file_id` to
your `POST /internal/payment-receipts`) — your finance contract already takes
`file_id`, so no change on your side.

## Tests
Hold on a shared test harness until Lead proposes one (likely a scripts-based
smoke + later userver testsuite). Keep your in-container smoke notes in
`docs/agent-b-<svc>-service.md` meanwhile.
