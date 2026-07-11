# notification-service

`notification-service` строит персональные in-app уведомления из уже
произошедших доменных событий. Он не является источником истины по занятиям,
заданиям, финансам или чату: его данные — производная projection.

[Вернуться к общей архитектуре](../../README.md)

## Что делает

- читает lesson, assignment, finance и chat topics;
- преобразует поддерживаемое событие в текст уведомления для нужного user;
- сохраняет список и unread-state в `notification_db`;
- защищается от повторной Kafka delivery;
- отдаёт уведомления через gRPC;
- помечает одно уведомление прочитанным;
- публикует `notification.created` для realtime push.

Email, Telegram, mobile push и массовая рассылка не реализованы.

## gRPC API

Контракт: [`notification.proto`](../../libs/proto/tutorflow/notification.proto).

| RPC | Назначение |
|---|---|
| `ListNotifications` | список пользователя, опционально только непрочитанные |
| `MarkAsRead` | отметить принадлежащее user уведомление прочитанным |

Gateway передаёт user context из JWT; нельзя прочитать или изменить чужое
уведомление по UUID.

## Какие события превращаются в уведомления

| Домен | Примеры | Получатель |
|---|---|---|
| Lessons | scheduled, completed, rescheduled, cancelled, restored | student |
| Assignments | created, submission uploaded, reviewed, deadline expired | student или teacher по смыслу события |
| Finance | receipt uploaded, payment confirmed/rejected | teacher или student |
| Balance | только `reason=correction.created` | student |
| Chat | `message.sent` | recipient |

Обычный `balance.changed` после charge/payment игнорируется, чтобы не создавать
дубликат уже понятного уведомления о занятии или подтверждённой оплате.

## Inbox, notification и outbox

Для поддерживаемого event один repository operation атомарно:

1. вставляет notification;
2. записывает `processed_events(event_id)`;
3. создаёт `notification.created` в outbox.

Дополнительный `UNIQUE(user_id, source_event_id)` защищает от дублирования на
уровне бизнес-данных. Повторный Kafka event не создаёт вторую строку.

Publisher отправляет `notification.created` в
`tutorflow.notification.events`; realtime-service доставляет его по WebSocket.
Если пользователь offline, сохранённое уведомление не теряется и доступно через
REST после reconnect.

## Данные

| Таблица | Назначение |
|---|---|
| `notifications` | текст, payload, source event и unread-state |
| `processed_events` | consumer inbox |
| `outbox_events` | события для realtime |

Сервис не делает JOIN с доменными базами. Payload события должен содержать всё,
что нужно для построения текста и выбора адресата.

## Внутренняя структура

```text
src/main.cpp
  ├── consumers/domain_event_consumer.*  event → notification mapping
  ├── domain/notification_service.*      user/domain validation
  ├── repositories/notification_repository.*
  ├── grpc/notification_grpc_service.*
  ├── outbox/outbox_publisher.*
  └── handlers/ready_handler.*
```

## Масштабирование

Реплики входят в consumer group `notification-domain-events`: Kafka делит
партиции между ними. Outbox publisher использует PostgreSQL advisory leader lock,
поэтому один batch публикует только одна реплика сервиса.

Масштабирование ограничено количеством партиций topic: лишняя consumer-реплика
останется без партиции.

## Runtime и проверка

Нужны `NOTIFICATION_DATABASE_URL`, Kafka consumer и producer. `/ready` проверяет
свою БД; временная недоступность Kafka обрабатывается retry-механизмами.

```bash
docker compose build notification-service
docker compose up -d notification-service api-gateway
python3 -m pytest tests/test_notifications.py tests/test_realtime.py -v
```

Источники:

- [protobuf](../../libs/proto/tutorflow/notification.proto);
- [event mapping](src/consumers/domain_event_consumer.cpp);
- [repository](src/repositories/notification_repository.cpp);
- [миграции](../../migrations/notification/);
- [каталог событий](../../docs/EVENTS.md).
