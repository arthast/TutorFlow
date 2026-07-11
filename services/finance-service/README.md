# finance-service

`finance-service` — единственный источник истины по начислениям, оплатам,
коррекциям, чекам и балансу. Реальных платёжных интеграций нет: преподаватель
вручную подтверждает или отклоняет загруженный учеником чек.

[Вернуться к общей архитектуре](../../README.md)

## Главная идея: append-only ledger

Финансовые операции не редактируются и не удаляются. Любое изменение выражается
новой строкой:

| Тип | Вклад в долг ученика |
|---|---:|
| `charge` | `+amount` |
| `payment` | `-amount` |
| `correction` | `+amount` или `-amount` |
| `refund` | `-amount` |

```text
balance = Σ charge − Σ payment + Σ correction − Σ refund
```

Положительный balance — долг, отрицательный — переплата. Полная модель описана
в [`docs/FINANCE_MODEL.md`](../../docs/FINANCE_MODEL.md).

## Возможности

- charge по завершённому занятию;
- получение баланса и журнала операций;
- создание ручной correction преподавателем;
- загрузка metadata чека после upload файла;
- список чеков по роли и status;
- подтверждение или отклонение teacher;
- компенсации при отмене/восстановлении completed lesson;
- Kafka inbox/outbox и защита от повторной доставки.

## gRPC API

Контракт: [`finance.proto`](../../libs/proto/tutorflow/finance.proto).

| RPC | Назначение |
|---|---|
| `CreateCharge` | внутреннее создание charge; обычный flow идёт из Kafka consumer |
| `GetBalance` | баланс с проверкой доступа |
| `ListTransactions` | append-only журнал |
| `CreatePaymentReceipt` | зарегистрировать загруженный чек |
| `ListPaymentReceipts` | список чеков |
| `ConfirmPaymentReceipt` | создать payment и изменить баланс |
| `RejectPaymentReceipt` | финально отклонить чек |
| `CreateCorrection` | ручная correction |

Gateway не публикует внешний endpoint создания charge. Нормальный источник
начисления — только событие `lesson.completed`.

## Начисление за занятие

```text
lesson-service
  → lesson.completed
  → Kafka tutorflow.lesson.events
  → LessonCompletedConsumer
  → charge + charge.created + balance.changed
  → finance_db/outbox
```

Уникальный индекс на `financial_transactions.lesson_id` для `type='charge'`
гарантирует один charge на занятие. `processed_events(event_id)` не даёт
повторно обработать тот же Kafka event.

## Чек и ручное подтверждение

```text
Student uploads bytes → file-service → file_id
Student creates receipt → pending_review
Teacher confirms:
  receipt → confirmed
  payment transaction is appended
  balance decreases
  payment.confirmed + balance.changed are written to outbox
```

Загрузка чека **не меняет баланс**. Изменение происходит только после
подтверждения преподавателем. Отклонение создаёт `payment.rejected`, но не
создаёт `payment`.

Повтор того же финального действия возвращает текущее состояние без второй
операции. Попытка поменять решение `confirmed ↔ rejected` возвращает conflict.

## Отмена и восстановление completed lesson

Исходный charge остаётся в журнале:

```text
complete                 charge(+price)
cancel completed         correction(-price)
restore completed        correction(+price)
```

Для `lesson.cancelled` и `lesson.restored` используется атомарный event-id
inbox: вставка `processed_events`, correction и `balance.changed` выполняется
одним SQL statement. Повторный event не создаёт вторую correction.

## Ручная correction

Teacher с активной связью может добавить положительную или отрицательную
correction с обязательным комментарием. Нулевое значение запрещено. Такая
операция не привязана к lesson и всегда добавляется новой строкой.

## Access control

- student читает только свой balance, transactions и receipts;
- teacher читает данные связанного student после identity access-check;
- подтвердить или отклонить чек может его teacher;
- сервис не читает `identity_db` напрямую.

## События

Потребляет `tutorflow.lesson.events`:

- `lesson.completed` → charge;
- `lesson.cancelled` с `previous_status=completed` → correction `-price`;
- `lesson.restored` → correction `+price`.

Публикует `tutorflow.finance.events`:

- `charge.created`;
- `payment_receipt.uploaded`;
- `payment.confirmed`;
- `payment.rejected`;
- `balance.changed`.

`balance.changed` несёт абсолютный balance после операции и delta для аудита.
Report-service хранит готовое значение и не пересчитывает бухгалтерию сам.

## Данные

| Таблица | Назначение |
|---|---|
| `financial_transactions` | append-only ledger |
| `payment_receipts` | состояние ручной проверки чеков |
| `processed_events` | consumer inbox |
| `outbox_events` | finance events для Kafka |

Критичные DB-гарантии:

- один charge на `lesson_id`;
- один payment на `receipt_id`;
- один consumer effect на `event_id`;
- status чека ограничен `pending_review/confirmed/rejected`.

## Внутренняя структура

```text
src/main.cpp
  ├── grpc/finance_grpc_service.*
  ├── domain/finance_service.*
  ├── repositories/finance_repository.*
  ├── consumers/lesson_completed_consumer.*
  ├── outbox/outbox_publisher.*
  └── handlers/ready_handler.*
```

## Runtime и проверка

Нужны `FINANCE_DATABASE_URL`, identity gRPC, Kafka consumer и producer.
`/ready` проверяет `finance_db`; Kafka восстанавливается через retry/outbox и не
входит в readiness.

```bash
docker compose build finance-service
docker compose up -d finance-service api-gateway
python3 -m pytest tests/test_finance.py tests/test_corrections.py -v
```

Источники:

- [финансовая модель](../../docs/FINANCE_MODEL.md);
- [protobuf](../../libs/proto/tutorflow/finance.proto);
- [domain service](src/domain/finance_service.cpp);
- [repository](src/repositories/finance_repository.cpp);
- [consumer](src/consumers/lesson_completed_consumer.cpp);
- [миграции](../../migrations/finance/).
