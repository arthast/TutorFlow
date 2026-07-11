# lesson-service

`lesson-service` — источник истины по доступным слотам, расписанию и жизненному
циклу занятий. Он хранит снимок цены занятия и публикует факты о произошедших
изменениях, но не создаёт финансовые начисления напрямую.

[Вернуться к общей архитектуре](../../README.md)

## Возможности

- создание и просмотр слотов доступности преподавателя;
- создание занятия вручную или на основе свободного слота;
- список занятий преподавателя или ученика;
- получение одного занятия;
- завершение, перенос, отмена и восстановление;
- материалы занятия как список `file_id`;
- запрет пересечения запланированных занятий преподавателя;
- публикация lifecycle events через transactional outbox.

## gRPC API

Контракт: [`lesson.proto`](../../libs/proto/tutorflow/lesson.proto).

| RPC | Назначение |
|---|---|
| `CreateAvailability` | создать свободный slot |
| `ListAvailability` | список slot-ов пользователя |
| `CreateLesson` | создать занятие |
| `ListLessons` | список по роли caller |
| `GetLesson` | получить занятие |
| `CompleteLesson` | завершить занятие |
| `RescheduleLesson` | перенести запланированное занятие |
| `ReactivateLesson` | восстановить отменённое занятие |
| `CancelLesson` | отменить scheduled или completed занятие |

Публичные REST-пути `/availability` и `/lessons*` находятся в gateway.

## Модель занятия

Статусы:

```text
scheduled ──complete──► completed
    │                      │
    └──────cancel──────────┴──► cancelled
                                      │
                        reactivate ────┤
                                      ├──► scheduled, если занятие не завершалось
                                      └──► completed, если completed_at уже был
```

Восстановление завершённого занятия возвращает именно `completed`, а не
`scheduled`. Это важно для финансовой компенсации: отмена completed создаёт
`correction(-price)`, а восстановление — `correction(+price)` в finance-service.

Повторный `complete`, `cancel` или допустимый `reactivate` возвращает уже
достигнутое состояние и не создаёт лишний доменный эффект.

## Цена

При создании занятия:

1. преподаватель и активная связь с учеником проверяются через identity;
2. явный `price` имеет приоритет;
3. если price не передан, используется `hourly_rate` из teacher-student link;
4. цена должна быть положительной;
5. значение сохраняется в `lessons.price` как снимок.

Последующее изменение ставки ученика не переписывает старые занятия.

## Защита от пересечений

`lesson_db` содержит PostgreSQL `EXCLUDE USING gist` constraint
`no_overlap_teacher` для полуоткрытых интервалов `[starts_at, ends_at)`.

- пересечение двух `scheduled` занятий одного teacher запрещено;
- соседние интервалы, например до 10:00 и с 10:00, разрешены;
- `completed` и `cancelled` не удерживают временной интервал;
- ограничение работает атомарно и закрывает гонку двух параллельных create или
  reschedule, которую нельзя надёжно решить одной предварительной проверкой.

Нарушение constraint преобразуется в `409 Conflict` общего error envelope.

## Transactional outbox

Изменение lesson и вставка строки `outbox_events` выполняются одним SQL
statement/транзакцией. Затем периодический publisher доставляет событие в
`tutorflow.lesson.events`.

| Событие | Когда возникает |
|---|---|
| `lesson.scheduled` | создание или восстановление незавершённого lesson |
| `lesson.completed` | первый переход scheduled → completed |
| `lesson.rescheduled` | реальное изменение времени/slot |
| `lesson.cancelled` | отмена scheduled или completed |
| `lesson.restored` | восстановление ранее завершённого lesson |

Kafka key — `lesson_id`, поэтому события одного занятия попадают в одну
партицию и сохраняют взаимный порядок.

## Завершение занятия: полный путь

```text
POST /lessons/{id}/complete
  → api-gateway
  → gRPC CompleteLesson
  → lesson-service проверяет ownership и status
  → lesson_db: status=completed + lesson.completed в outbox
  → Kafka
  → finance-service создаёт charge
  → notification/report обновляют производные данные
```

Ответ содержит `charge_status: pending`, потому что charge создаётся
асинхронно. Gateway и lesson-service не должны создавать его сами.

## Данные

Сервис владеет `lesson_db`:

| Таблица | Назначение |
|---|---|
| `availability_slots` | интервалы `open/booked` |
| `lessons` | расписание, статус, price snapshot, `completed_at` |
| `lesson_files` | ссылки `file_id` на материалы |
| `outbox_events` | события, ожидающие Kafka publication |

UUID пользователей не имеют foreign key в `identity_db`; межсервисные JOIN
запрещены.

## Внутренняя структура

```text
src/main.cpp
  ├── grpc/lesson_grpc_service.*
  ├── domain/lesson_service.*
  ├── repositories/lesson_repository.*
  ├── outbox/outbox_publisher.*
  └── handlers/ready_handler.*
```

`LessonService` выполняет role/access checks и подготавливает команду.
`LessonRepository` держит state transitions, атомарный SQL и outbox рядом с
данными, которые должны измениться согласованно.

## Runtime и проверка

Нужны `LESSON_DATABASE_URL`, identity gRPC и Kafka producer settings. `/ready`
проверяет собственную PostgreSQL БД; Kafka producer ретраит отдельно и не входит
в readiness.

```bash
docker compose build lesson-service
docker compose up -d lesson-service api-gateway
python3 -m pytest tests/test_overlap.py tests/test_corrections.py -v
```

Источники:

- [protobuf](../../libs/proto/tutorflow/lesson.proto);
- [domain service](src/domain/lesson_service.cpp);
- [repository](src/repositories/lesson_repository.cpp);
- [миграции](../../migrations/lesson/);
- [события](../../docs/EVENTS.md).
