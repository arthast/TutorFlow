# api-gateway CORS

## Context
- Stage: `docs/roadmap.md` 1.4, CORS before frontend integration.
- Scope: api-gateway only, no domain contract changes.

## Implementation
- Added api-gateway CORS helper that sets:
  - `Access-Control-Allow-Origin` from `GATEWAY_CORS_ORIGIN`;
  - `Access-Control-Allow-Methods: GET, POST, OPTIONS`;
  - `Access-Control-Allow-Headers: Authorization, Content-Type, X-Request-Id`;
  - `Access-Control-Max-Age: 600`.
- `OPTIONS` requests on configured gateway routes return `204` locally, without JWT validation and without upstream proxying.
- Gateway-owned CORS headers override any upstream `Access-Control-*` headers.
- `/health` now uses an api-gateway-local health handler so health responses get the same CORS headers.

## Configuration
- `gateway-settings.cors-origin` is read from `GATEWAY_CORS_ORIGIN`.
- `docker-compose.yml` passes `GATEWAY_CORS_ORIGIN` to api-gateway with dev default `http://localhost:5173`.
- `.env.example` documents `GATEWAY_CORS_ORIGIN=http://localhost:5173`.

## Verification
- Build/run commands used by this task should include:
  - `docker compose build api-gateway`
  - `docker compose up -d --no-deps api-gateway`
  - `curl -i -X OPTIONS http://localhost:8080/me`
  - `curl -i http://localhost:8080/me -H "Authorization: Bearer <token>"`
  - `GATEWAY_CORS_ORIGIN=http://localhost:9999 docker compose up -d --no-deps --force-recreate api-gateway`
