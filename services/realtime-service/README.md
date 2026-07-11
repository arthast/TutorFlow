# realtime-service

`realtime-service` — публичный WebSocket push-слой для чата, read markers,
presence и уведомлений. Он не принимает доменные команды и не является
источником истины.

[Вернуться к общей архитектуре](../../README.md)

## Публичный endpoint

```text
ws://localhost:8089/ws?token=<jwt>
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

## Kafka → Redis → WebSocket

```text
chat/notification events
  → Kafka consumer group realtime-domain-events
  → realtime instance, которому назначена partition
  → Redis rt:user:{user_id} pub/sub
  → все realtime instances
  → instance с локальным connection
  → WebSocket
```

Consumer слушает `tutorflow.chat.events` и
`tutorflow.notification.events`:

- `message.sent` кэширует участников, увеличивает best-effort unread counter и
  отправляет `chat.message`;
- `message.read` сбрасывает realtime counter reader и отправляет peer событие
  `chat.read`;
- `notification.created` отправляет `notification` адресату.

## Почему нужен Redis

WebSocket connection живёт в памяти конкретного процесса. Kafka event может
получить другая реплика. Redis pub/sub переносит push на instance, где открыт
нужный connection, без sticky routing Kafka partitions к пользователям.

Используемые данные:

| Ключ/канал | Назначение |
|---|---|
| `rt:presence:{user}` | presence с TTL 45 секунд |
| `rt:user:{user}` | pub/sub channel персональной доставки |
| `rt:unread:{user}:{dialog}` | быстрый badge counter, TTL 24 часа |
| `rt:dialog:{dialog}:participants` | cache участников для read delivery |
| `rt:user_peers:{user}` | известные peer для presence fan-out |

Unread в Redis — не источник истины. После reconnect frontend получает
каноническое состояние через `chat-service.ListDialogs`.

## Connection lifecycle

1. handshake проверяет JWT;
2. connection добавляется в локальный `ConnectionRegistry`;
3. presence key обновляется и peers получают online;
4. server отправляет WebSocket ping раз в 15 секунд;
5. client `ping` обновляет Redis TTL и получает `pong`;
6. при закрытии connection удаляется, presence очищается и публикуется offline.

Если клиент пропустил событие во время disconnect, WebSocket replay отсутствует.
Он должен перечитать сообщения/notifications через REST. Persistent state не
зависит от доступности realtime-service.

## Состояние и надёжность

У сервиса нет PostgreSQL:

- локальная память хранит только активные connections и outbound queues;
- Redis хранит временный fan-out/presence/cache state;
- Kafka offsets принадлежат consumer group;
- domain data остаётся в chat/notification сервисах.

`/ready` проверяет Redis. Kafka не входит в readiness и восстанавливается через
consumer retry. Redis pub/sub не является durable queue, поэтому REST
resynchronization обязательна.

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
| `REDIS_URL` | Redis endpoint |
| `KAFKA_SECDIST_CONFIG` | Kafka brokers |

```bash
docker compose build realtime-service
docker compose up -d redis realtime-service api-gateway
python3 -m pytest tests/test_realtime.py tests/test_chat.py -v
```

Источники:

- [WebSocket handler](src/ws/websocket_handler.cpp);
- [Kafka consumer](src/kafka/realtime_event_consumer.cpp);
- [Redis client](src/redis/redis_client.cpp);
- [protocol notes](../../docs/agent-realtime-service.md);
- [event contracts](../../docs/event-contracts/).
