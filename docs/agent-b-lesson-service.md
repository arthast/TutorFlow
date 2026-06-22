# Agent B Notes — lesson-service

Last updated: 2026-06-22.

## Scope

Owner: Agent B.

Service: `services/lesson-service`.

Contract source: `docs/api-contracts/lesson.openapi.yaml`.

Do not touch: identity-service, file-service, api-gateway. Cross-service calls are
behind local client interfaces with temporary stubs until the owning services are
ready.

## Implemented

- Added userver/PostgreSQL-backed lesson-service components.
- Added local layers:
  - `src/clients/` — `IdentityClient` and `FinanceClient` interfaces;
    identity is still stubbed, finance uses HTTP.
  - `src/domain/` — lesson domain service and local DTO/model JSON mapping.
  - `src/repositories/` — SQL for `availability_slots` and `lessons`.
  - `src/handlers/` — thin HTTP handlers with common error envelope.
- Wired components in:
  - `services/lesson-service/src/main.cpp`
  - `services/lesson-service/configs/static_config.yaml`
  - `services/lesson-service/CMakeLists.txt`
- Implemented endpoints:
  - `GET /health`
  - `POST /internal/availability`
  - `GET /internal/availability`
  - `POST /internal/lessons`
  - `GET /internal/lessons`
  - `GET /internal/lessons/{lessonId}`
  - `POST /internal/lessons/{lessonId}/complete`
  - `POST /internal/lessons/{lessonId}/cancel`

## Behavior

- `POST /internal/lessons` requires teacher auth headers and calls
  `IdentityClient::CheckAccess`.
- The current `IdentityClient` is a stub that returns `{allowed: true,
  status: active}`.
- `POST /internal/lessons/{lessonId}/complete`:
  - allows only the owning teacher;
  - rejects cancelled lessons;
  - returns already completed lessons unchanged;
  - calls `FinanceClient::CreateCharge` after completion.
- `FinanceClient` calls finance-service `POST /internal/charges` through
  `FINANCE_SERVICE_URL`.
- Cancelling a scheduled lesson reopens its slot when the lesson has a `slot_id`.
- Responses and errors use JSON content type and common error envelope.

## Known Contract Gap

`CreateLessonRequest.price` is nullable in `lesson.openapi.yaml` and says that
missing price should come from identity `hourly_rate`.

Current identity contract for `POST /internal/relations/check-access` returns only:

```json
{ "allowed": true, "status": "active" }
```

It does not expose `hourly_rate`, while `migrations/lesson/001_init.sql` has
`lessons.price NUMERIC(12, 2) NOT NULL`.

Current runtime decision: if `price` is missing, lesson-service returns:

```http
422 Unprocessable Entity
```

```json
{
  "error": {
    "code": "business_rule",
    "message": "price is required until identity exposes relation hourly_rate",
    "details": {
      "contract_gap": "identity check-access does not expose hourly_rate"
    }
  }
}
```

Proposed Lead decision for later: either extend `CheckAccessResponse` with
`hourly_rate` or add a separate internal identity endpoint to fetch the
teacher-student relation snapshot.

## Verified

Commands used:

```bash
docker compose build lesson-service
docker compose up -d postgres lesson-service
./scripts/migrate.sh lesson
docker compose exec -T lesson-service sh -lc 'curl -sS -i http://localhost:8082/health'
```

Smoke path verified inside the `lesson-service` container:

1. Create availability slot.
2. Create lesson with explicit `price`.
3. Complete lesson.
4. Repeat complete request.

Observed result: repeated complete returned the same `completed` lesson and did
not change lesson state.

Also verified:

- `GET /internal/lessons` returns `Content-Type: application/json; charset=utf-8`.
- Missing `price` returns `422 business_rule` envelope.

Finance integration smoke verified after wiring `HttpFinanceClient`:

1. Create lesson with explicit `price`.
2. Complete lesson.
3. Repeat complete request.
4. Read finance balance and transactions for the smoke student.

Observed result: finance balance became `321.0`, and finance transactions
contained exactly one `charge` for the completed lesson even after repeated
complete.

Latest integration smoke ids:

- lesson: `67dbfeeb-045c-4114-ab9c-044ee4f2b339`
- charge: `a0761125-e746-4ff8-8091-c181e0e51b4b`
- teacher: `11111111-2222-4333-8444-555555555555`
- student: `22222222-3333-4444-8555-666666666666`

## Next Steps

- Replace `StubIdentityClient` with an HTTP client once identity check-access is
  available.
- Revisit the price contract gap with Lead before changing OpenAPI or identity
  response shape.
- Add focused tests or script smoke once the repo has an agreed test harness.
- Continue with assignment-service internal endpoints once this branch is merged.
