# Gateway receipts list

Stage: 2.5.5.

Changes (api-gateway only):
- `static_config.yaml`: `gateway-payment-receipts-handler` method is now
  `GET,POST,OPTIONS` (was `POST,OPTIONS`).
- `PaymentReceiptsHandler` proxies the request to finance
  `/internal/payment-receipts`. GET and POST share the same internal path, so the
  handler is a single `ProxyToUpstream` call — `ProxyToUpstream` already routes by
  HTTP method (GET vs POST) and appends the original query suffix, so
  `?status=pending_review` is forwarded unchanged. (No per-method `if` branch is
  needed here, unlike `StudentsHandler` where GET/POST map to different paths.)
- Auth is unchanged: gateway validates Bearer JWT, strips incoming `X-User-*`,
  and forwards `X-User-Id` / `X-User-Roles` derived from the token.

Validation (re-run 2026-06-23):
- `docker compose build api-gateway` -> OK.
- `docker compose up -d` + `GET /health` -> 200.
- `GET /payments/receipts?status=pending_review` as teacher -> 200, array with
  the created pending receipt (count=1).
- `GET /payments/receipts?status=bogus` -> 400 `validation_error`, which confirms
  the query string reaches finance.
- `GET /payments/receipts` as student -> 200, but empty array (see limitation).
- `python3 scripts/smoke_mvp.py` -> `SMOKE OK`.
- `python3 -m pytest tests/ -q` -> 13 passed.

Known limitation OUTSIDE this task boundary (api-gateway only):
- `GET /payments/receipts` as student returns `200 []` for a newly created own
  receipt, because finance-service `ListReceipts` filters by
  `teacher_id = X-User-Id`. The DoD "student sees own receipts" therefore is not
  fully met by a gateway-only change — it needs a finance-side fix (scope by
  `student_id` when the caller is a student). The finance contract summary already
  says this list is "teacher — на проверку", so this should be a separate,
  coordinator-approved finance task, not a gateway change.
