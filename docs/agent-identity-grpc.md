# Identity gRPC migration notes

Stage: 5B.

## Scope

- `identity-service` now exposes `IdentityService` over gRPC using the fixed
  `libs/proto/tutorflow/identity.proto` contract.
- `api-gateway` calls identity over gRPC for auth, current user, student list,
  student detail, and student creation. External REST paths and JSON response
  shapes are preserved.
- `lesson-service`, `assignment-service`, and `finance-service` use the shared
  gRPC `IdentityClient` for teacher-student access checks.
- `file-service` remains on the HTTP identity client for now. It is intentionally
  left unchanged in 5B and can be cleaned up after 5C.

## gRPC methods

Implemented in identity:

- `Register`
- `Login`
- `ValidateToken`
- `ChangePassword`
- `GetUser`
- `GetStudent`
- `ListStudents`
- `CreateStudent`
- `CheckTeacherStudentAccess`

`ChangePassword` uses the authenticated user from gRPC metadata
(`x-user-id` / `x-user-roles`) and keeps the REST domain behavior for current
password validation.

## Error policy

Domain `ServiceError` is mapped to standard gRPC status codes:

- `400` -> `INVALID_ARGUMENT`
- `401` -> `UNAUTHENTICATED`
- `403` -> `PERMISSION_DENIED`
- `404` -> `NOT_FOUND`
- `409` -> `ALREADY_EXISTS`
- `422` -> `FAILED_PRECONDITION`
- other errors -> `INTERNAL`

`CreateStudent` duplicate email is returned as `ALREADY_EXISTS`. The gRPC status
details carry the stable code `email_taken`, so gateway can preserve the external
REST envelope code.

Gateway maps gRPC failures back to the existing REST envelope:

```json
{"error":{"code":"...","message":"...","details":null}}
```

## Client policy

The shared gRPC identity client lives in `libs/clients` next to the HTTP client.
It keeps the existing `IdentityClient` interface for `CheckAccess`, so callers
consume the same `{allowed, status, hourly_rate}` semantics.

Identity access checks are idempotent and use the gRPC client base call options,
including default deadline, retry policy for idempotent calls, and propagation
of request/user metadata.

## Deprecated REST

The internal REST identity endpoints are not removed in 5B. They are marked as
deprecated in identity static config comments and remain available while the rest
of the system migrates.
