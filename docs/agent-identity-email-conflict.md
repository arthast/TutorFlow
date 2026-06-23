# Identity: email conflict on student creation

Stage: roadmap 1.6.

`POST /students` creates a new student user through identity-service. If the
requested email already exists, PostgreSQL raises `UniqueViolation` on
`users.email`. `CreateStudentWithLink` maps that exception to a `409` response
with `error.code = "email_taken"`.

No API contract changes were made in this task: the `409` response for
`/internal/students` was already present in `docs/api-contracts`.
