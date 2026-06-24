# Этап 5A: gRPC foundation

## Что добавлено

- `libs/proto/tutorflow/*.proto` — versioned proto-пакеты `tutorflow.*.v1`.
- `libs/proto/CMakeLists.txt` — генерация C++/gRPC/userver адаптеров через `userver_add_grpc_library`.
- `tutorflow_grpc_clients` — отдельная lib с общим transport helper для будущих gRPC-клиентов; существующий `tutorflow_clients` оставлен HTTP-only.
- `identity-service`, `lesson-service`, `assignment-service`, `finance-service` получили стандартный `grpc-server` + `grpc-health` без бизнес RPC handlers.

## Proto policy

- `common.proto` содержит только инфраструктурные типы: `UserContext`, `Empty`, paging.
- `identity.proto` содержит RPC-сигнатуры этапа 5B без реализации.
- `lesson.proto`, `assignment.proto`, `finance.proto` — скелеты будущей миграции 5C.
- Ошибки передаются стандартными gRPC status codes:
  `INVALID_ARGUMENT`, `UNAUTHENTICATED`, `PERMISSION_DENIED`, `NOT_FOUND`,
  `ALREADY_EXISTS`, `FAILED_PRECONDITION`, `INTERNAL`.
- Кастомный `ErrorDetails` не вводился как основной механизм ошибок.

## Client policy

- Базовый deadline/timeout: 5 секунд.
- Retry включается только при явном `GrpcOperationKind::kIdempotent`.
- Для неидемпотентных операций выставляется ровно 1 attempt.
- Metadata пробрасывает `x-request-id`, `x-trace-id`, `X-User-Id`, `X-User-Roles`.
- `MapGrpcStatusToServiceError` переводит gRPC status в текущий REST envelope через `ServiceError`; исходный gRPC status сохраняется в `details.grpc_status`.

## Что не делалось

- Сервисы не переведены на gRPC; текущий REST/HTTP транспорт живёт параллельно.
- Gateway, frontend и file-service не мигрировались.
- gRPC health не включался в gateway и file-service: gateway остаётся внешним REST edge, file-service по roadmap остаётся HTTP для upload/download.
- Kafka/outbox не добавлялись.
