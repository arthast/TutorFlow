# MVP smoke test

## Scope
- Stage: `docs/roadmap.md` 1.5.
- Script: `scripts/smoke_mvp.py`.
- It uses only Python stdlib and sends all requests through api-gateway.

## Run
```bash
docker compose up --build
./scripts/migrate.sh all
python3 scripts/smoke_mvp.py
```

Optional gateway URL override:
```bash
GATEWAY_URL=http://localhost:8080 python3 scripts/smoke_mvp.py
```

On success the script prints:
```text
SMOKE OK
```

On failure it prints the failed step, expected/actual HTTP status or field check,
the response body when available, and exits with a non-zero code.

## Current verification
Strict run through `http://localhost:8080` currently reaches step 12 and fails on:
```text
POST /payments/receipts expected [201], got 400
{"error":{"code":"validation_error","message":"missing required field: teacher_id","details":{}}}
```

The smoke request body follows `docs/api-contracts/gateway.openapi.yaml`
(`file_id`, `amount`, `currency`, `comment`). The current finance implementation
behind gateway still requires `teacher_id` and `student_id`; that needs a service
or gateway alignment before the full 15-step smoke can print `SMOKE OK`.
