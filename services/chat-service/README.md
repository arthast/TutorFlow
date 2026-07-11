# chat-service

`chat-service` — источник истины по личным диалогам teacher-student, сообщениям,
вложениям и read markers. Запись сообщений идёт через REST/gateway/gRPC;
WebSocket используется только для доставки уже созданных событий.

[Вернуться к общей архитектуре](../../README.md)

## Возможности

- find-or-create диалога для активной пары teacher-student;
- список диалогов пользователя;
- отправка текстового сообщения и/или `file_id` attachments;
- пагинированное чтение сообщений;
- monotonic read marker;
- события `message.sent` и `message.read`;
- два PostgreSQL shard по `dialog_id`.

Групповые чаты, edit/delete, поиск, реакции и история участников не реализованы.

## gRPC API

Контракт: [`chat.proto`](../../libs/proto/tutorflow/chat.proto).

| RPC | Назначение |
|---|---|
| `CreateDialog` | идемпотентно получить диалог пары |
| `ListDialogs` | список и unread counters |
| `SendMessage` | создать сообщение |
| `ListMessages` | читать историю с pagination cursor |
| `MarkRead` | сдвинуть read marker вперёд |

Каждая команда проверяет роль, участие в диалоге и при создании — активную
связь через identity-service.

## Детерминированный dialog ID

Для пары вычисляется UUIDv5:

```text
UUIDv5(namespace,
       lower(teacher_id) + ":" + lower(student_id))
```

Одна и та же пара всегда получает один `dialog_id`, даже после рестарта.
`INSERT ... ON CONFLICT DO NOTHING` делает создание идемпотентным без
глобального каталога и распределённого unique index.

## Шардирование

Текущая конфигурация содержит два shard:

```text
shard = FNV-1a(dialog_id bytes) % 2
```

На выбранном shard вместе находятся:

- dialog;
- messages;
- attachments;
- read markers;
- outbox events этого dialog.

Поэтому `SendMessage` остаётся локальной PostgreSQL-транзакцией без distributed
transaction. Kafka key также равен `dialog_id`, сохраняя порядок сообщений
диалога в topic partition.

### Scatter-gather

Запросы внутри одного dialog сразу идут на один shard. Но `ListDialogs(user)` не
знает dialog IDs заранее, поэтому выполняется на обоих shard; результаты
объединяются, дедуплицируются и сортируются по `last_message_at`.

Для двух shard и десятков диалогов это простой компромисс. При большом числе
shard понадобится отдельный user-dialog index или read-model.

### Ограничение решардинга

Изменение `% 2` на другое число поменяет placement существующих dialog. Online
resharding и consistent hashing не реализованы; смена числа shard требует
отдельной миграции данных.

Подробнее: [ADR 0002](../../docs/adr/0002-chat-db-sharding.md).

## Отправка сообщения

```text
POST /chats/{dialogId}/messages
  → gateway → gRPC SendMessage
  → participant/content/file_id checks
  → выбранный shard:
       message + attachments + dialogs.last_message_at + outbox
  → Kafka message.sent
  ├── notification-service → persistent notification
  └── realtime-service → chat.message WebSocket push
```

В event передаётся короткий preview, но не полный текст как универсальная копия
домена. Attachments остаются в file-service.

## Read marker

Read marker пользователя движется только вперёд. `message.read` публикуется,
только если указатель реально изменился. Повторный запрос не создаёт лишний
event и не возвращает unread назад.

## Данные на каждом shard

| Таблица | Назначение |
|---|---|
| `dialogs` | участники и время последнего сообщения |
| `messages` | sender, text, timestamp |
| `message_attachments` | ссылки на file-service |
| `read_markers` | последний прочитанный message пользователя |
| `outbox_events` | события dialog |

Схема из `migrations/chat/` применяется отдельно к `chat_db_shard0` и
`chat_db_shard1`.

## Внутренняя структура

```text
src/main.cpp
  ├── grpc/chat_grpc_service.*
  ├── domain/chat_service.*
  ├── repositories/chat_repository.*
  ├── repositories/shard_router.*
  ├── repositories/dialog_merge.*
  ├── outbox/outbox_publisher.*  отдельный publisher на shard
  └── handlers/ready_handler.*
```

## Runtime и проверка

Нужны `CHAT_DATABASE_URL_SHARD0`, `CHAT_DATABASE_URL_SHARD1`, identity gRPC и
Kafka producer. `/ready` проверяет оба shard.

```bash
docker compose build chat-service
docker compose up -d chat-service api-gateway realtime-service
python3 -m pytest tests/test_chat.py tests/test_realtime.py -v
cmake --build cmake-build-debug --target chat-sharding-unit-tests
ctest --test-dir cmake-build-debug -R chat-sharding --output-on-failure
```

Источники:

- [protobuf](../../libs/proto/tutorflow/chat.proto);
- [domain service](src/domain/chat_service.cpp);
- [repository](src/repositories/chat_repository.cpp);
- [shard router](src/repositories/shard_router.cpp);
- [ADR](../../docs/adr/0002-chat-db-sharding.md).
