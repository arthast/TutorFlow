# identity temp-password flow implementation notes

Branch: `feat/gateway-routing-auth`
Status: DONE

---

## What was implemented

Scope: `identity-service` and `api-gateway` only. No migrations were added.

identity-service:
- `POST /internal/students` now requires `email`, `password`, and
  `display_name`.
- The created student stores the real email and a PBKDF2-HMAC-SHA256 hash of
  the temporary password.
- The created `teacher_student_links` row is inserted with `status = active`.
- Added `POST /internal/auth/change-password`.
  - Reads current user from `X-User-Id` set by gateway.
  - Body: `{ "current_password": "...", "new_password": "..." }`.
  - Invalid current password returns `401`.
  - `new_password` shorter than 8 characters returns `400`.
  - Success returns `{ "status": "ok" }`.

api-gateway:
- Added protected `POST /auth/change-password`.
- Gateway validates Bearer JWT locally, strips inbound `X-User-*`, sets
  `X-User-Id` / `X-User-Roles` from JWT, then proxies to
  identity `/internal/auth/change-password`.

## Components

identity-service:
- `identity-change-password-handler`

api-gateway:
- `gateway-auth-change-password-handler`

## Verification

Expected smoke path:
1. Teacher logs in.
2. Teacher calls `POST /students` with `email`, `password`, `display_name`.
3. Student logs in with email + temporary password.
4. Student calls `POST /auth/change-password`.
5. Old password login returns `401`; new password login succeeds.
6. `GET /me` with the student token returns the student user.
