# Agent A — identity-service implementation notes

Branch: `feat/A-identity`  
Commit: `85e7778` (feat(identity): implement full identity-service)  
Status: **DONE — ready for PR**

---

## What was implemented

### libs/common additions (additive, don't break B)
- `libs/common/include/tutorflow/common/jwt.hpp` — `Claims` struct + `Sign` / `Verify`
- `libs/common/src/jwt.cpp` — HS256 via OpenSSL HMAC-SHA256 + base64url (manual,
  no extra deps). `Sign` creates header.payload.sig. `Verify` validates sig and exp.
- `libs/common/CMakeLists.txt` — added `jwt.cpp`, linked `OpenSSL::Crypto` as PRIVATE,
  added `find_package(OpenSSL REQUIRED)`.

### Contract changes
- `docs/api-contracts/identity.openapi.yaml` — `CheckAccessResponse` extended with
  nullable `hourly_rate` (closes lesson price gap — see contract-gap memory).

### identity-service (port 8081)

**Endpoints implemented:**

| Method | Path | Handler |
|--------|------|---------|
| POST | `/internal/auth/register` | `RegisterHandler` |
| POST | `/internal/auth/login` | `LoginHandler` |
| GET | `/internal/users/{userId}` | `GetUserHandler` |
| POST | `/internal/relations/check-access` | `CheckAccessHandler` |
| POST | `/internal/students` | `CreateStudentHandler` |
| GET | `/internal/students/{studentId}` | `GetStudentLinkHandler` |
| GET | `/internal/teachers/{teacherId}/students` | `ListStudentsHandler` |

**Password hashing:** PBKDF2-HMAC-SHA256, 100k iterations, 16-byte salt, 32-byte hash.
Stored as `pbkdf2$100000$<salt_hex>$<hash_hex>` in `users.password_hash`.

**JWT:** HS256, secret from `JWT_SECRET` env, TTL 86400s (configurable via
`jwt-expires-in-seconds` in static config). Payload: `{sub, roles, iat, exp}`.

**check-access** returns `{allowed, status, hourly_rate}`. `hourly_rate` is
`teacher_student_links.hourly_rate` cast to `double precision`.

**CreateStudent** (teacher-scoped via X-User-Id header):
- Requires `display_name`, `email` is optional.
- If no email: placeholder `noemail+<uuid>@placeholder.internal` via `gen_random_uuid()` in SQL.
- Atomically creates `users` + `student_profiles` + `teacher_student_links` in one CTE query.

**Component names** (for static_config.yaml):
- `identity-repository`
- `identity-domain-service`  ← has `jwt-secret#env: JWT_SECRET`
- `identity-register-handler`, `identity-login-handler`
- `identity-get-user-handler`, `identity-check-access-handler`
- `identity-create-student-handler`, `identity-get-student-handler`, `identity-list-students-handler`

**DB component name:** `identity-db` (env: `IDENTITY_DATABASE_URL`)

---

## What Agent B needs to do after rebase

1. Update `AccessCheckResult` in all three services' `identity_client.hpp` to add
   `std::optional<double> hourly_rate` field.
2. In lesson-service: use `hourly_rate` from check-access response when
   `CreateLessonRequest.price` is null (closes the 422 business_rule gap).

---

## Known limitations (MVP)

- `GET /internal/auth/validate-token` not implemented (gateway validates locally).
- `GetStudentLink` returns the most recent link for a given student_id (student can
  have multiple teachers; MVP returns first by created_at DESC).
- Teacher-created student accounts have empty `password_hash` — can't login until
  a separate reset/set-password flow exists (out of scope for MVP).

---

## Next tasks (Lead roadmap)

4. **file-service** — binary upload/download, access check via check-access
5. **api-gateway** — JWT validate locally, strip/set X-User-*, route to internal services
