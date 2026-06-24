# Этап 5F: расширение Kafka event layer

5F добавляет не новые бизнес-flow, а инфраструктурное закрепление событий:
общий outbox publisher, новые доменные события assignment/finance и inbox для
идемпотентности finance consumer.

## 1. Shared outbox publisher

Общая реализация вынесена в `libs/events`:

- `tutorflow/events/outbox_publisher.hpp`;
- `libs/events/src/outbox_publisher.cpp`.

`PostgresOutboxPublisher` ожидает одинаковую таблицу `outbox_events`, читает
`pending` строки, публикует `EventEnvelope` в topic `tutorflow.<event_type>` и
после успешной публикации помечает строку как `published`.

Семантика осталась **at-least-once**: сначала publish, потом mark-as-published.
Дубликаты допустимы, консьюмеры обязаны быть идемпотентными.

`lesson-service`, `assignment-service` и `finance-service` используют один общий
publisher, но сохраняют свои component names:

- `lesson-outbox-publisher`;
- `assignment-outbox-publisher`;
- `finance-outbox-publisher`.

## 2. Assignment events

В `assignment_db` добавлена таблица `outbox_events`
(`migrations/assignment/002_outbox_events.sql`).

Assignment repository пишет события атомарно с доменным изменением:

- `assignment.created` при создании assignment;
- `submission.uploaded` при загрузке submission;
- `assignment.reviewed` при ревью последней submission.

События описывают уже случившийся факт. Валидация доступа, статусов и файловый
flow остались прежними.

## 3. Finance events

В `finance_db` добавлены:

- `outbox_events` (`migrations/finance/002_outbox_events.sql`);
- `processed_events` (`migrations/finance/003_processed_events.sql`).

Finance repository пишет:

- `charge.created` при фактическом создании charge;
- `payment_receipt.uploaded` при загрузке чека;
- `payment.confirmed` при фактическом создании payment;
- `payment.rejected` при отклонении чека;
- `balance.changed` при событиях, которые меняют баланс.

Повторные идемпотентные операции не создают вторые события: например, повторный
confirm уже подтвержденного receipt не создаёт второй `payment.confirmed` и второй
`balance.changed`.

## 4. Inbox/idempotency

Finance consumer `lesson.completed` теперь использует `processed_events`.
Если `event_id` уже обработан, событие пропускается. Если сервис упадёт после
создания charge, но до записи `processed_events`, доменная идемпотентность
`unique(lesson_id)` всё равно защищает от второго charge при повторной доставке.

Итого:

- идемпотентность по `event_id` — быстрый skip уже обработанных событий;
- идемпотентность по `lesson_id` — защита доменной операции от replay/crash.

## 5. Event contracts

Контракты лежат в `docs/event-contracts/*.v1.json`. Naming convention:
`<domain>.<past_tense>`, topic convention: `tutorflow.<event_type>`.

Добавлены контракты:

- `submission.uploaded.v1.json`;
- `assignment.reviewed.v1.json`;
- `payment_receipt.uploaded.v1.json`;
- `payment.rejected.v1.json`;
- `charge.created.v1.json`;
- `balance.changed.v1.json`.

## Проверка

```sh
COMPOSE_PARALLEL_LIMIT=1 BUILD_JOBS=2 docker compose build \
  lesson-service assignment-service finance-service
docker compose up --force-recreate migrator
docker compose up -d lesson-service assignment-service finance-service api-gateway
python3 scripts/smoke_mvp.py
python3 -m pytest tests
```

Состояние outbox/inbox можно проверить так:

```sh
docker compose exec postgres psql -U tutorflow -d assignment_db -c \
  "select event_type, status, count(*) from outbox_events group by 1,2 order by 1,2;"

docker compose exec postgres psql -U tutorflow -d finance_db -c \
  "select event_type, status, count(*) from outbox_events group by 1,2 order by 1,2; \
   select event_type, count(*) from processed_events group by 1 order by 1;"
```

## Что НЕ делалось

- Notification/report/chat consumers не добавлялись.
- Kafka не используется для команд.
- REST/gRPC внешние контракты и frontend не менялись.
- `file-service` не переводился на события.
