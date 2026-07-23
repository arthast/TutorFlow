# realtime-service

`realtime-service` — публичный WebSocket push-слой для чата, read markers,
presence и уведомлений. Он не принимает доменные команды и не является
источником истины.

[Вернуться к общей архитектуре](../../README.md)

## Публичный endpoint

```text
ws://localhost:8089/ws?token=<jwt>
ws://localhost:8090/ws?token=<jwt>  # вторая dev-реплика
```

Browser WebSocket API не позволяет штатно передать `Authorization`, поэтому JWT
находится в query parameter. Handshake локально проверяет подпись и `sub` тем же
`JWT_SECRET`, который используют identity и gateway.

В production Caddy проксирует `/ws` на этот сервис; остальные API routes идут
в gateway.

## Server → client

Формат:

```json
{"type":"chat.message","payload":{"dialog_id":"..."}}
```

| `type` | Источник | Назначение |
|---|---|---|
| `chat.message` | `message.sent` | новое сообщение recipient |
| `chat.read` | `message.read` | собеседник продвинул read marker |
| `notification` | `notification.created` | новое persistent уведомление |
| `presence` | Redis presence updates | online/offline известного peer |
| `pong` | client `ping` | heartbeat response |

Из client messages реализован `ping`. Создание сообщений, mark-read и любые
финансовые/учебные команды идут через REST API.

## Kafka → Redis → две WebSocket-реплики

```text
chat/notification events
  → Kafka consumer group realtime-domain-events
  → realtime instance, которому назначена partition
  ├→ локальный ConnectionRegistry
  └→ Redis rt:user:{user_id} Pub/Sub с origin instance
  → обе realtime replicas
  → instance с локальным connection
  → WebSocket
```

В dev Compose работают два одинаковых процесса: первый опубликован на `8089`,
второй — на `8090`. Они используют одну Kafka consumer group, поэтому событие
обрабатывает только одна реплика. Сначала она доставляет сообщение своим
локальным соединениям, затем публикует его в Redis. Остальные реплики получают
публикацию и доставляют её своим WebSocket. Реплика-источник игнорирует свой
`origin`, поэтому локальный клиент не получает дубль.

Consumer слушает `tutorflow.chat.events` и
`tutorflow.notification.events`:

- `message.sent` кэширует участников, увеличивает best-effort unread counter и
  отправляет `chat.message`;
- `message.read` сбрасывает realtime counter reader и отправляет peer событие
  `chat.read`;
- `notification.created` отправляет `notification` адресату.

## Почему нужен Redis и как он подключён

WebSocket connection живёт в памяти конкретного процесса. Kafka event может
получить другая реплика. Redis pub/sub переносит push на instance, где открыт
нужный connection, без sticky routing Kafka partitions к пользователям.

Сервис использует штатный модуль `userver::redis`:

- `components::Redis` управляет соединениями и reconnect;
- `storages::redis::Client` выполняет typed-команды;
- `SubscribeClient::Psubscribe` слушает `rt:user:*`;
- `SubscriptionToken` управляет lifecycle подписки.

В TutorFlow остаётся небольшой `RedisClient` adapter. Он знает имена ключей,
TTL, Pub/Sub envelope и правила presence, но не открывает сокеты вручную.

Используемые данные:

| Ключ/канал | Назначение |
|---|---|
| `rt:presence:{user}` | sorted set активных connection leases |
| `rt:user:{user}` | pub/sub channel персональной доставки |
| `rt:unread:{user}:{dialog}` | быстрый badge counter, TTL 24 часа |
| `rt:dialog:{dialog}:participants` | cache участников для read delivery |
| `rt:user_peers:{user}` | известные peer для presence fan-out |

Unread в Redis — не источник истины. После reconnect frontend получает
каноническое состояние через `chat-service.ListDialogs`.

## Connection lifecycle

1. handshake проверяет JWT;
2. connection добавляется в локальный `ConnectionRegistry`;
3. в Redis добавляется lease конкретного connection;
4. peers получают `online`, только если это первое активное соединение;
5. server отправляет WebSocket ping раз в 15 секунд;
6. client `ping` продлевает свой lease и получает `pong`;
7. при закрытии удаляется только lease этого connection;
8. peers получают `offline`, только если закрыто последнее соединение.

Поэтому пользователь не становится offline при закрытии одной вкладки, если
другая вкладка или соединение со второй репликой ещё активно. Просроченные
leases удаляются атомарными Redis scripts при следующей presence-операции.

У каждого connection есть собственная неограниченная
`userver::concurrent::MpscQueue`. Kafka/Redis callbacks добавляют сообщения как
producers, а WebSocket handler остаётся единственным consumer. Handler через
`WaitAny` одновременно ждёт queue signal, готовность WebSocket для чтения и
следующий heartbeat deadline. Поэтому idle connection больше не просыпается
каждые 100 мс, а `SendText` и `TryRecv` по-прежнему выполняются одной корутиной,
что безопасно и для TLS-соединений.

Если клиент пропустил событие во время disconnect, WebSocket replay отсутствует.
Он должен перечитать сообщения/notifications через REST. Persistent state не
зависит от доступности realtime-service.

## Состояние и надёжность

У сервиса нет PostgreSQL:

- локальная память хранит только активные connections и outbound queues;
- Redis хранит временный fan-out/presence/cache state;
- Kafka offsets принадлежат consumer group;
- domain data остаётся в chat/notification сервисах.

`/ready` проверяет command-соединение Redis. Kafka не входит в readiness и
восстанавливается через consumer retry. После Redis restart command client может
стать ready немного раньше, чем завершится повторный `PSUBSCRIBE`; отдельный
reconnect smoke проверяет eventual cross-instance delivery. Redis Pub/Sub не
является durable queue, поэтому REST resynchronization обязательна.

## Внутренняя структура

```text
src/main.cpp
  ├── ws/websocket_handler.*       JWT, heartbeat, lifecycle
  ├── ws/connection_registry.*     local connections
  ├── kafka/realtime_event_consumer.* event → push mapping
  ├── redis/redis_client.*         presence, cache, pub/sub
  └── handlers/ready_handler.*
```

## Runtime и проверка

| Переменная | Назначение |
|---|---|
| `JWT_SECRET` | WebSocket auth |
| `REDIS_HOST` | Redis host для structured secdist |
| `REDIS_PORT` | Redis port, обычно `6379` |
| `REDIS_PASSWORD` | Redis password, пустой в dev |
| `REALTIME_SECDIST_CONFIG` | объединённые Kafka и Redis settings userver |
| `REALTIME_PORT` | host port первой dev-реплики, обычно `8089` |
| `REALTIME_REPLICA_PORT` | host port второй dev-реплики, обычно `8090` |

```bash
docker compose build realtime-service
docker compose up -d redis realtime-service realtime-service-replica api-gateway
python3 -m pytest tests/test_realtime.py tests/test_chat.py -v
python3 scripts/smoke_realtime_redis_reconnect.py
```

Источники:

- [WebSocket handler](src/ws/websocket_handler.cpp);
- [Kafka consumer](src/kafka/realtime_event_consumer.cpp);
- [Redis client](src/redis/redis_client.cpp);
- [protocol notes](../../docs/agent-realtime-service.md);
- [event contracts](../../docs/event-contracts/).
