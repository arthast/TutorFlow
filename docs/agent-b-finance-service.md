# Agent B notes: finance-service

## Scope

Service owner: Agent B.

Implemented branch: `feat/finance-internal`.

Contract source: `docs/api-contracts/finance.openapi.yaml`.

Do not touch: identity-service, file-service, api-gateway. Current external
dependency is represented by a local identity client that calls identity by
OpenAPI contract.

## Implemented

- `/health` returns common health response.
- Internal finance endpoints:
  - `POST /internal/charges`
  - `GET /internal/students/{studentId}/balance`
  - `GET /internal/students/{studentId}/transactions`
  - `POST /internal/payment-receipts`
  - `GET /internal/payment-receipts?status=...`
  - `POST /internal/payment-receipts/{receiptId}/confirm`
  - `POST /internal/payment-receipts/{receiptId}/reject`
- PostgreSQL repository, domain layer, thin HTTP handlers.
- `IdentityClient` calls identity-service
  `POST /internal/relations/check-access` through `IDENTITY_SERVICE_URL`.
- Finance journal remains append-only for balance-changing operations:
  `charge`, `payment`, `correction`, `refund`.
- Charge creation is idempotent by `unique(lesson_id)`.
- Receipt confirmation is idempotent by a partial unique index on
  `transactions.receipt_id` for `type = 'payment'`.
- Receipt upload does not change balance. Balance changes only when a teacher
  confirms a receipt.
- lesson-service is wired to finance-service through `FinanceHttpClient` and
  calls `POST /internal/charges` on lesson `complete`.

## Domain decisions

- Balance formula is scoped to RUB for MVP:
  `charge - payment + correction - refund`.
- `POST /internal/payment-receipts` requires `X-User-Id` and only allows the
  student to create their own receipt.
- `GET /internal/payment-receipts` requires `X-User-Id` and returns receipts for
  the teacher from the header, optionally filtered by status.
- `confirm` and `reject` require `X-User-Id` as teacher id and enforce teacher
  ownership of the receipt.
- Re-confirming an already confirmed receipt returns the existing confirmed
  receipt and does not create a second payment transaction.
- Re-rejecting an already rejected receipt returns the existing rejected receipt.
- Rejecting a confirmed receipt returns `409 conflict`; confirming a rejected
  receipt returns `409 conflict`.

## Verification

Commands run:

```bash
docker compose build finance-service
docker compose up -d postgres finance-service
./scripts/migrate.sh finance
docker compose exec finance-service curl -s -i http://localhost:8084/health
```

Smoke path run with test ids:

- teacher: `aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa`
- student: `bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb`
- lesson: `cccccccc-cccc-cccc-cccc-cccccccccccc`
- file: `dddddddd-dddd-dddd-dddd-dddddddddddd`

Checked:

- first charge returns `201 Created`;
- repeated charge returns `200 OK` with the same transaction id;
- balance after charge is `1500.0`;
- receipt upload returns `pending_review`;
- balance after upload is still `1500.0`;
- first confirm returns `confirmed`;
- repeated confirm returns `confirmed` and does not duplicate payment;
- balance after confirm is `500.0`;
- student transactions contain exactly one `charge` and one `payment`;
- receipt listing by `status=confirmed` returns the confirmed receipt;
- rejecting a confirmed receipt returns envelope error `409 conflict`.

Identity client wiring verified after replacing the stub:

```bash
docker compose build lesson-service assignment-service finance-service
docker compose up -d identity-service lesson-service assignment-service finance-service
docker compose exec finance-service curl -s -i http://localhost:8084/health
```

Current integration limitation: this branch's identity-service still has only
`/health`, so finance paths that require `POST /internal/relations/check-access`
will receive an upstream 404 envelope until Agent A merges the identity endpoint.

## Current contract notes

No finance contract changes were made.

## Next work

1. Re-run finance receipt/list/confirm happy-path with real identity check-access
   once Agent A merges the identity endpoint.
2. Add focused tests or script smoke once the repo has an agreed test harness.
