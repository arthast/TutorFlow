# Agent B Notes — lesson-service

Last updated: 2026-06-22.

## Scope

Owner: Agent B.

Service: `services/lesson-service`.

Contract source: `docs/api-contracts/lesson.openapi.yaml`.

Do not touch: identity-service, file-service, api-gateway. Cross-service calls are
behind local client interfaces and call owning services by OpenAPI contract.

## Implemented

- Added userver/PostgreSQL-backed lesson-service components.
- Added local layers:
  - `src/clients/` — `IdentityClient` and `FinanceClient` interfaces;
    both use HTTP clients.
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
- `IdentityClient` calls identity-service
  `POST /internal/relations/check-access` through `IDENTITY_SERVICE_URL`.
- If `CreateLessonRequest.price` is missing, lesson-service uses `hourly_rate`
  from identity `check-access`. It returns `422 business_rule` only when both
  request `price` and relation `hourly_rate` are missing.
- `POST /internal/lessons/{lessonId}/complete`:
  - allows only the owning teacher;
  - rejects cancelled lessons;
  - returns already completed lessons unchanged;
  - calls `FinanceClient::CreateCharge` after completion.
- `FinanceClient` calls finance-service `POST /internal/charges` through
  `FINANCE_SERVICE_URL`.
- Cancelling a scheduled lesson reopens its slot when the lesson has a `slot_id`.
- Responses and errors use JSON content type and common error envelope.

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
- Missing `price` uses identity relation `hourly_rate`; if the relation has no
  rate, lesson-service returns `422 business_rule` envelope.

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

Identity client wiring verified after replacing the stub:

```bash
docker compose build lesson-service assignment-service finance-service
docker compose up -d identity-service lesson-service assignment-service finance-service
docker compose exec lesson-service curl -s -i http://localhost:8082/health
```

Stage 1.2 update:

- `IdentityClient::CheckAccess` parses nullable `hourly_rate`.
- Creating a lesson without explicit `price` uses `hourly_rate` from
  check-access.
- Completing such a lesson creates a finance charge for that derived price.
- Verified through gateway: creating a lesson without `price` for a relation with
  `hourly_rate = 777.0` returned `201` with `lessons.price = 777.0`; completing
  it created a finance `charge` for `777.0`.
- Verified through gateway: creating a lesson without `price` for a relation
  without `hourly_rate` returned `422 business_rule`.
- `python3 scripts/smoke_mvp.py` returned `SMOKE OK`.
