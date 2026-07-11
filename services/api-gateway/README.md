# api-gateway

`api-gateway` — публичный REST-фасад TutorFlow. Браузер не знает адреса
внутренних доменных сервисов и отправляет все обычные API-запросы сюда.

Gateway отвечает за внешний HTTP-контракт, аутентификацию, CORS, преобразование
JSON ↔ protobuf и маршрутизацию. Бизнес-решения остаются внутри доменных
сервисов: gateway не рассчитывает баланс, не меняет состояния занятий и не
создаёт финансовые операции.

[Вернуться к общей архитектуре](../../README.md)

## Место в системе

```text
React frontend
    │ REST/JSON
    ▼
api-gateway
    ├── gRPC ──────────────► identity / lesson / assignment / finance
    │                       notification / report / chat
    └── HTTP multipart ────► file-service
```

`realtime-service` является отдельным публичным WebSocket-входом. Он доставляет
push-события, но доменные команды всё равно проходят через gateway.

## Что делает

- публикует REST API из
  [`gateway.openapi.yaml`](../../docs/api-contracts/gateway.openapi.yaml);
- проверяет JWT локально с общим `JWT_SECRET`;
- удаляет присланные клиентом заголовки `X-User-*` и формирует доверенный
  пользовательский контекст из JWT;
- вызывает внутренние gRPC API сервисов;
- проксирует upload/download файлов в `file-service` по HTTP multipart;
- преобразует доменные/gRPC-ошибки в единый JSON envelope;
- обрабатывает CORS и preflight-запросы `OPTIONS`;
- отдаёт `/health`, `/ready` и внутренние Prometheus-метрики.

## Чего здесь нет

- собственной базы данных;
- Kafka producer/consumer;
- доменных сущностей и бизнес-транзакций;
- прямого доступа к базам других сервисов;
- WebSocket-соединений.

## Публичные группы endpoint-ов

| Область | Основные пути | Внутренний получатель |
|---|---|---|
| Auth и профиль | `/auth/*`, `/me` | `identity-service` |
| Ученики | `/students*` | `identity`, `finance`, `report` |
| Занятия | `/availability`, `/lessons*` | `lesson-service` |
| Домашние задания | `/assignments*` | `assignment-service` |
| Финансы | `/payments/receipts*`, `/students/*/transactions` | `finance-service` |
| Уведомления | `/notifications*` | `notification-service` |
| Дашборды | `/dashboard/*`, `/students/*/summary` | `report-service` |
| Файлы | `/files*` | `file-service` по HTTP |
| Чат | `/chats*` | `chat-service` |

Полные request/response schema находятся в OpenAPI, а не дублируются здесь.

## Аутентификация и доверенный контекст

1. `identity-service` подписывает JWT с `sub`, `roles`, `iat`, `exp`.
2. Клиент передаёт `Authorization: Bearer <token>`.
3. Gateway проверяет подпись и срок действия локально — отдельный сетевой вызов
   для каждого запроса не нужен.
4. Любые входящие `X-User-*` считаются недоверенными и отбрасываются.
5. В gRPC-запрос передаётся `UserContext`; при HTTP-вызове file-service gateway
   формирует доверенные `X-User-Id` и `X-User-Roles`.
6. Конечный сервис повторно выполняет доменную проверку: например, связь
   преподавателя с учеником проверяется через identity gRPC.

То есть gateway подтверждает, **кто** делает запрос, а доменный сервис решает,
**разрешена ли конкретная операция**.

## Внутренняя структура

```text
src/main.cpp
  ├── регистрирует userver-компоненты и handlers
  ├── handlers/proxy_handlers.*
  │     REST parsing, auth, mapping, response
  ├── clients/*_grpc_client.*
  │     typed gRPC-вызовы доменных сервисов
  ├── gateway_settings.*
  │     JWT, CORS, file URL, timeout
  ├── cors.*
  └── handlers/health_handler.* / ready_handler.*
```

Handler должен оставаться тонким: разобрать HTTP, вызвать клиент, преобразовать
результат. Если в handler появляется расчёт баланса или state machine занятия,
это нарушение границы gateway.

## Зависимости и конфигурация

| Настройка | Назначение |
|---|---|
| `JWT_SECRET` | локальная проверка токена |
| `GATEWAY_CORS_ORIGIN` | разрешённый origin frontend |
| `FILE_SERVICE_URL` | HTTP-адрес file-service |
| gRPC endpoints в `static_config.yaml` | адреса семи gRPC-сервисов |
| `timeout-ms` | timeout внутренних вызовов, по умолчанию 5000 мс |

В конфигурации используются `dns:///service:port`: в Kubernetes это позволяет
gRPC-клиенту работать с несколькими pod через DNS и round-robin policy.

## Health и readiness

- `/health` проверяет, что процесс и HTTP listener живы;
- `/ready` отражает готовность gateway принимать трафик;
- `/metrics` находится на отдельном monitor listener `18080` и не публикуется
  наружу в обычной конфигурации.

Недоступность отдельного downstream не превращается в liveness failure gateway:
внутренние клиенты возвращают контролируемую upstream-ошибку.

## Как проверить

Из корня репозитория:

```bash
docker compose build api-gateway
docker compose up -d api-gateway
curl http://localhost:8080/health
curl http://localhost:8080/ready
python3 -m pytest tests/test_health.py tests/test_auth.py tests/test_access.py -v
```

Основные источники:

- [публичный OpenAPI](../../docs/api-contracts/gateway.openapi.yaml);
- [регистрация компонентов](src/main.cpp);
- [handlers](src/handlers/proxy_handlers.cpp);
- [gRPC clients](src/clients/);
- [общая архитектура](../../docs/architecture.md).
