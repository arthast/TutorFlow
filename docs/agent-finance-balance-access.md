# Finance: access control on balance / transactions (IDOR fix)

Stage: 2.5.6. Changed **finance-service only**. Contract unchanged in shape — only
the `403` behavior is added (already allowed by the error envelope).

## Problem

`GET /internal/students/{studentId}/balance` and `.../transactions` took the
`studentId` from the path and did NOT check the caller — any authenticated user
could read anyone's balance/transactions (IDOR).

## Change

- `FinanceService` gained a private helper `EnsureStudentAccess(auth, student_id)`:
  - allow if `auth.user_id == student_id` (the student themselves);
  - else allow if `identity_.CheckAccess(auth.user_id, student_id).allowed`
    (the caller is a teacher linked to that student);
  - else throw `ServiceError::Forbidden` (`403`, unified envelope).
- `GetBalance` / `ListTransactions` now take `const AuthContext&` and call the
  helper before hitting the repository.
- Handlers `GetBalanceHandler` / `ListTransactionsHandler` parse auth via
  `tutorflow::common::ParseAuthContext(request)` and pass it down. The gateway
  already forwards `X-User-Id` / `X-User-Roles` for these routes
  (`StudentBalanceHandler` / `StudentTransactionsHandler` authenticate first).

`CreateCharge` (called by lesson-service, no `X-User-*`) was intentionally left
untouched — it parses no auth and must keep working for internal charges.

Files:
- `services/finance-service/src/domain/finance_service.hpp`
- `services/finance-service/src/domain/finance_service.cpp`
- `services/finance-service/src/handlers/finance_handlers.cpp`
- `tests/test_access.py` (new `test_finance_balance_transactions_access`)

## Validation

- `docker compose build finance-service` -> OK; `up -d finance-service`; `/health` -> 200.
- For both `balance` and `transactions`:
  - student reads own -> 200; another student's -> 403;
  - linked teacher reads own student's -> 200; another teacher's student -> 403.
  - 403 body: `{"error":{"code":"forbidden","message":"not allowed to access this student's finance data","details":{}}}`.
- `python3 scripts/smoke_mvp.py` -> `SMOKE OK`.
- `python3 -m pytest tests/ -q` -> 14 passed.
