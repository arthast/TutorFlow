# Refactor R1: shared libs

Scope: mechanical refactor only. Contracts, behavior, and service
`static_config.yaml` files were not changed.

Changes:
- Moved duplicated JSON handler helpers to
  `libs/common/include/tutorflow/common/handler_helpers.hpp`.
- Added `libs/clients` with shared
  `tutorflow::clients::IdentityClient` and `HttpIdentityClient`.
- Switched lesson, assignment, finance, and file services to the shared identity
  client component while keeping component name `identity-client`.
- Removed local `clients/identity_client.*` copies from lesson, assignment,
  finance, and file services.

Verification target:
- `docker compose build`
- `python3 scripts/smoke_mvp.py`
- `python3 -m pytest tests/ -v`
