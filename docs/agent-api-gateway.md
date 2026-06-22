# api-gateway implementation notes

Branch: `feat/gateway-routing-auth`
Status: DONE

---

## What was implemented

api-gateway is a thin external proxy for the public contract in
`docs/api-contracts/gateway.openapi.yaml`.

Responsibilities:
- `POST /auth/register` proxies to identity `POST /internal/auth/register`
  without requiring JWT.
- `POST /auth/login` proxies to identity `POST /internal/auth/login`
  without requiring JWT.
- All other public handlers verify `Authorization: Bearer <JWT>` locally via
  `tutorflow::common::jwt::Verify` and `JWT_SECRET`.
- Gateway strips any inbound `X-User-*` headers and sets:
  - `X-User-Id: <sub>`
  - `X-User-Roles: <roles CSV>`
- Gateway also propagates/generates `X-Request-Id`.
- Gateway errors use the common envelope format.
- Upstream responses, including upstream error envelopes and binary bodies, are
  proxied with the upstream status and body.

## Components

Settings component:
- `gateway-settings`

Handler components:
- `gateway-auth-register-handler`
- `gateway-auth-login-handler`
- `gateway-me-handler`
- `gateway-students-handler`
- `gateway-student-handler`
- `gateway-student-balance-handler`
- `gateway-student-transactions-handler`
- `gateway-availability-handler`
- `gateway-lessons-handler`
- `gateway-lesson-handler`
- `gateway-lesson-complete-handler`
- `gateway-lesson-cancel-handler`
- `gateway-assignments-handler`
- `gateway-assignment-handler`
- `gateway-assignment-submit-handler`
- `gateway-assignment-review-handler`
- `gateway-assignment-comments-handler`
- `gateway-payment-receipts-handler`
- `gateway-payment-receipt-confirm-handler`
- `gateway-payment-receipt-reject-handler`
- `gateway-files-handler`
- `gateway-file-meta-handler`
- `gateway-file-download-handler`

## Route map

Auth:
- `/auth/register` -> identity `/internal/auth/register`
- `/auth/login` -> identity `/internal/auth/login`

Identity:
- `/me` -> `/internal/users/{X-User-Id}`
- `/students` GET -> `/internal/teachers/{X-User-Id}/students`
- `/students` POST -> `/internal/students`
- `/students/{studentId}` -> `/internal/students/{studentId}`

Finance:
- `/students/{studentId}/balance` -> `/internal/students/{studentId}/balance`
- `/students/{studentId}/transactions` -> `/internal/students/{studentId}/transactions`
- `/payments/receipts` -> `/internal/payment-receipts`
- `/payments/receipts/{receiptId}/confirm` -> `/internal/payment-receipts/{receiptId}/confirm`
- `/payments/receipts/{receiptId}/reject` -> `/internal/payment-receipts/{receiptId}/reject`

Lesson:
- `/availability` -> `/internal/availability`
- `/lessons*` -> `/internal/lessons*`

Assignment:
- `/assignments*` -> `/internal/assignments*`

File:
- `/files` -> `/internal/files`
- `/files/{fileId}` -> `/internal/files/{fileId}`
- `/files/{fileId}/download` -> `/internal/files/{fileId}/download`

## Known limitations

- Gateway does not do business orchestration or domain validation.
- Gateway does not stream file uploads/downloads yet; it forwards the userver
  request/response body as a string, matching the current MVP services.
- Upstream unavailability/timeouts return `503` with common `internal_error`
  code because `libs/common` has no dedicated upstream error code.
