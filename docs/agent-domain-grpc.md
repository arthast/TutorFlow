# Этап 5C: domain services на gRPC

Stage: 5C. Предусловие: 5A (gRPC foundation) и 5B (identity на gRPC) сделаны.

## Scope (что сделано)

- `lesson-service`, `assignment-service`, `finance-service` теперь поднимают
  gRPC-серверы (`LessonService` / `AssignmentService` / `FinanceService`) по
  своим зафиксированным `libs/proto/tutorflow/{lesson,assignment,finance}.proto`.
  Доменная логика НЕ дублируется — gRPC-хэндлеры — тонкая обёртка над
  существующими domain-компонентами (`*-domain-service`).
- `api-gateway` ходит в lesson/assignment/finance по gRPC через новые клиент-
  компоненты (`gateway-{lesson,assignment,finance}-grpc-client`) по образцу
  `gateway-identity-grpc-client` из 5B. Внешний REST JSON и error envelope
  для фронта НЕ изменены.
- gRPC health у всех трёх сервисов уже был включён в 5A (`SERVING`).

## Маппинг RPC → доменные методы

| Сервис | RPC | Доменный метод |
|---|---|---|
| lesson | CreateAvailability | `CreateSlot` |
| lesson | ListAvailability | `ListSlots` |
| lesson | CreateLesson / ListLessons / GetLesson / CompleteLesson / CancelLesson | одноимённые |
| assignment | CreateAssignment / ListAssignments / GetAssignment / SubmitAssignment / ReviewAssignment | одноимённые |
| assignment | AddComment | `CreateComment` |
| finance | CreateCharge | `CreateCharge` (реализован для полноты контракта; см. ниже) |
| finance | GetBalance / ListTransactions | одноимённые |
| finance | CreatePaymentReceipt | `CreateReceipt` (student_id из auth-метаданных) |
| finance | ListPaymentReceipts | `ListReceipts` (скоуп по роли + фильтр `status`) |
| finance | ConfirmPaymentReceipt / RejectPaymentReceipt | `ConfirmReceipt` / `RejectReceipt` |

Семантика сохранена: роли (`RequireTeacher`, скоуп списков по роли),
идемпотентность `CompleteLesson`→charge и `CreateCharge`, state-machine чека
(confirm/reject + 409 на смену финального решения), `EnsureStudentAccess` на
balance/transactions — всё это живёт в domain-слое и не менялось.

## Auth-контекст

gRPC-хэндлеры читают auth из метаданных `x-user-id` / `x-user-roles`
(фоллбэк на `UserContext` в теле запроса, если метаданных нет). Gateway-клиенты
прокидывают эти метаданные через `MakeGrpcCallOptions` (как identity-клиент) и
дополнительно заполняют `UserContext` в запросе.

## Маппинг ошибок

Доменный `ServiceError` → gRPC status code (как в 5B):

- `400` → INVALID_ARGUMENT
- `401` → UNAUTHENTICATED
- `403` → PERMISSION_DENIED
- `404` → NOT_FOUND
- `409` → ALREADY_EXISTS
- `422` → FAILED_PRECONDITION
- прочее → INTERNAL

Gateway переводит gRPC-ошибки обратно во внешний REST envelope
`{error:{code,message,details}}` через `MapGrpcStatusToServiceError` (общий
`libs/clients/grpc_client_base`). HTTP-статусы внешних ответов (201 на
create/slot/lesson/assignment/submit/comment/receipt; 200 на остальных)
выставляются gateway вручную и совпадают с прежним REST-поведением upstream.

## Что намеренно НЕ делалось (по скоупу)

- **lesson → finance charge** при `CompleteLesson` остаётся внутренним REST
  (`HttpFinanceClient` → `POST /internal/charges`). Будет заменён Kafka-событием
  на 5E. Поэтому finance `/internal/charges` НЕ помечен deprecated.
- **Файлы** (`gateway → file-service`, multipart) остаются на HTTP — не трогали.
- **file-service check-access** не трогали (остаток после 5B, отдельная задача).
- proto-контракты, frontend, внешний REST gateway (пути/тело/envelope) — без
  изменений. Kafka/outbox не добавлялись.

## Deprecated REST

Старые internal REST-эндпоинты lesson/assignment/finance помечены комментарием
`# Deprecated after 5C` в `configs/static_config.yaml` — НЕ удалены, остаются
рабочими на время миграции. Исключения (НЕ deprecated): finance
`/internal/charges` (нужен lesson→finance) и файловые эндпоинты file-service.

## Build / run / test

Сборка посервисно (сборка всех сразу упирается в память Docker — OOM):

```sh
docker compose build lesson-service
docker compose build assignment-service
docker compose build finance-service
docker compose build api-gateway
```

Прогон:

```sh
docker compose up -d
python3 scripts/smoke_mvp.py       # -> SMOKE OK
python3 -m pytest tests            # все проходят без правок тестов
```

## Пострефакторинг 5C (cleanup + DRY)

После 5C проведена чистка структуры:

- **Общий gRPC-код вынесен в `libs/clients` (target `tutorflow_grpc_clients`):**
  - `grpc_server_utils.{hpp,cpp}` — `ResolveServerAuthContext` (метаданные →
    `AuthContext`), `ServiceErrorToGrpcStatus`, `InvokeServerUnary<Response>`.
    Используется всеми 4 gRPC-серверами (identity/lesson/assignment/finance)
    вместо локальных копий boilerplate.
  - `grpc_client_base` дополнен `InvokeUnary`, `IdempotentCall`/`NonIdempotentCall`,
    `FillUserContext` — общий клиентский boilerplate для всех gateway-клиентов.
  - gateway: `clients/json_helpers.hpp` (`NullableString`, `StringArray`,
    `RequireStringArray`) — общие JSON-edge-хелперы для 4 gateway-клиентов.
- **file-service переведён на gRPC identity** (`GrpcIdentityClient`,
  `dns:///identity-service:9081`). HTTP `identity_client` (`HttpIdentityClient` +
  `identity_client.cpp`) удалён; остался только интерфейс `IdentityClient` в
  `identity_client.hpp`. Target `tutorflow_clients` (HTTP-only) удалён.
- **Удалён мёртвый internal REST** lesson/assignment/finance/identity (обработчики,
  их `ToJson`/`Parse`, регистрации в main/static_config/CMake). Исключения,
  оставшиеся рабочими: finance `POST /internal/charges` (lesson→finance до 5E) и
  HTTP file upload/download.

## Внутренние вызовы после 5C

Единственные оставшиеся внутренние REST-вызовы (ожидаемо):

- `lesson → finance` charge (`POST /internal/charges`) — до 5E (Kafka);
- `gateway → file-service` upload/download (multipart) — остаётся HTTP.

Всё остальное (gateway → identity/lesson/assignment/finance,
lesson/assignment/finance → identity check-access) ходит по gRPC.
