# Agent Note: notification-service (Stage 5G)

Дата: 2026-06-24.

## Что реализовано

- Новый `notification-service` на C++20/userver:
  - PostgreSQL БД `notification_db`;
  - gRPC API `NotificationService.ListNotifications` и `MarkAsRead`;
  - стандартный gRPC health;
  - Kafka consumer доменных событий.
- Новый proto-контракт `libs/proto/tutorflow/notification.proto`.
- Gateway вызывает notification-service по gRPC и отдаёт внешний REST:
  - `GET /notifications?unread_only=true`;
  - `POST /notifications/{notificationId}/read`.
- Frontend показывает in-app уведомления в кабинетах teacher/student и умеет
  помечать уведомления прочитанными.

## События

Consumer слушает:

```text
tutorflow.assignment.created
tutorflow.submission.uploaded
tutorflow.assignment.reviewed
tutorflow.payment_receipt.uploaded
tutorflow.payment.confirmed
tutorflow.payment.rejected
tutorflow.lesson.completed
```

Маппинг получателя:

```text
assignment.created       -> student
submission.uploaded      -> teacher
assignment.reviewed      -> student
payment_receipt.uploaded -> teacher
payment.confirmed        -> student
payment.rejected         -> student
lesson.completed         -> student
```

## Идемпотентность

В `notification_db` есть:

- `processed_events(event_id primary key, event_type, processed_at)`;
- `notifications.source_event_id`;
- `unique(user_id, source_event_id)`.

Повторный replay Kafka-события не создаёт дубль уведомления.

## Что намеренно не делалось

- Email/Telegram/push-уведомления.
- WebSocket/SSE и realtime-счётчики.
- Chat-service и message events.
- Report/read-model dashboards.
- Изменение бизнес-логики доменных сервисов: notification-service только читает
  факты из Kafka и строит свою локальную read-model.

## Проверка

```bash
COMPOSE_PARALLEL_LIMIT=1 BUILD_JOBS=2 docker compose build notification-service api-gateway
docker compose up -d notification-service api-gateway
curl http://localhost:8080/health
python3 scripts/smoke_mvp.py
python3 -m pytest tests
npm --prefix frontend run build
```
