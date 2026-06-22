# finance receipt auth alignment

## Scope
- Stage: `docs/roadmap.md` 1.7.
- Changed only finance-service receipt creation path and `scripts/smoke_mvp.py`.
- Contracts were not changed.

## Change
- `POST /internal/payment-receipts` now takes `student_id` from `X-User-Id`
  via `tutorflow::common::ParseAuthContext`.
- Request body still provides `teacher_id`, `file_id`, `amount`, and optional
  `currency` and `comment`.
- The domain behavior remains the same: receipt starts as `pending_review`, upload
  does not change balance, and confirm/reject/list still use teacher auth from
  `X-User-Id`.
- Smoke step 12 now sends `teacher_id` when creating a payment receipt.

## Verification
- `docker compose build finance-service`
- `docker compose up -d --no-deps --force-recreate finance-service`
- `./scripts/migrate.sh all`
- `python3 scripts/smoke_mvp.py`

Result: `SMOKE OK`, including repeated lesson complete idempotency, receipt
confirm payment creation, and final balance `charge - payment = 0`.
