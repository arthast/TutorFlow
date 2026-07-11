# assignment-service

`assignment-service` — источник истины по домашним заданиям, решениям,
проверкам и комментариям внутри задания.

[Вернуться к общей архитектуре](../../README.md)

## Возможности

- преподаватель создаёт ДЗ для связанного ученика;
- к условию прикладываются `file_id`;
- ученик отправляет текст, файлы или оба вида ответа;
- преподаватель проверяет последнее решение;
- участники оставляют комментарии в контексте задания;
- просроченные задания автоматически переходят в `expired`;
- основные изменения публикуются через transactional outbox.

Комментарии задания намеренно находятся здесь, а не в `chat-service`: они
принадлежат aggregate домашнего задания и отображаются вместе с его историей.

## gRPC API

Контракт: [`assignment.proto`](../../libs/proto/tutorflow/assignment.proto).

| RPC | Назначение |
|---|---|
| `CreateAssignment` | создать ДЗ |
| `ListAssignments` | список для teacher/student |
| `GetAssignment` | detail с submissions и comments |
| `SubmitAssignment` | отправить или повторно отправить решение |
| `ReviewAssignment` | проверить последнее решение |
| `AddComment` | добавить комментарий участника |

## Доступ

- создать ДЗ может teacher с активной связью с student;
- отправить решение может только назначенный student;
- review выполняет только владелец-teacher;
- detail и comments доступны только двум участникам задания;
- проверка связи при создании идёт через identity gRPC;
- после создания ownership проверяется по данным aggregate в `assignment_db`.

## Статусы

Assignment:

```text
assigned → submitted → reviewed / needs_fix / done
    ▲                         │
    └──── новая submission ───┘

assigned или needs_fix + прошедший due_at → expired
```

Submission использует `submitted`, `reviewed`, `needs_fix`, `accepted`.
Допустимые review-команды: `reviewed`, `needs_fix`, `accepted`. При accepted
родительское задание приходит в финальное состояние, соответствующее текущей
repository-логике.

Нельзя отправить решение для `done` или `expired`; expired assignment нельзя
проверить.

## Повторная сдача

Новая submission не перезаписывает старую строку: история решений сохраняется.
`ReviewLatestSubmission` работает с последней сдачей. Файлы решения находятся в
file-service, а здесь хранится только уникальная пара `(submission_id, file_id)`.

## Deadline worker

`DeadlineWorker` запускается периодически, по умолчанию каждые 60 секунд:

1. выбирает assignments со статусом `assigned` или `needs_fix` и прошедшим
   `due_at`;
2. использует `FOR UPDATE SKIP LOCKED`, поэтому параллельные worker не берут одну
   строку одновременно;
3. переводит строку в `expired`;
4. в той же транзакции вставляет `assignment.deadline_expired` в outbox.

`submitted`, `reviewed`, финальные состояния и задания без `due_at` worker не
трогает. После первого перехода строка больше не подходит условию, поэтому
повторный tick идемпотентен.

Период задаёт `ASSIGNMENT_DEADLINE_WORKER_PERIOD_MS`.

## События

| Событие | Смысл |
|---|---|
| `assignment.created` | преподаватель выдал ДЗ |
| `submission.uploaded` | ученик создал новую submission |
| `assignment.reviewed` | преподаватель проверил решение |
| `assignment.deadline_expired` | worker зафиксировал просрочку |

События публикуются в `tutorflow.assignment.events` с ключом
`assignment_id`. Consumers — notification-service и report-service.

## Данные

Сервис владеет `assignment_db`:

| Таблица | Назначение |
|---|---|
| `assignments` | условие, участники, due_at и общий status |
| `assignment_files` | файлы условия |
| `submissions` | история решений |
| `submission_files` | файлы конкретной submission |
| `assignment_comments` | контекстные комментарии |
| `outbox_events` | события для Kafka |

Нет FK к identity/file базам: только стабильные UUID.

## Внутренняя структура

```text
src/main.cpp
  ├── grpc/assignment_grpc_service.*
  ├── domain/assignment_service.*
  ├── repositories/assignment_repository.*
  ├── outbox/outbox_publisher.*
  ├── workers/deadline_worker.*
  └── handlers/ready_handler.*
```

## Пример потока

```text
Student → POST /assignments/{id}/submit
  → gateway → gRPC SubmitAssignment
  → role + ownership + content validation
  → submission + submission_files + outbox
  → submission.uploaded
  → notification/report projections
```

## Runtime и проверка

Нужны `ASSIGNMENT_DATABASE_URL`, identity gRPC, Kafka producer settings и
настройка периода worker. `/ready` проверяет собственную БД.

```bash
docker compose build assignment-service
docker compose up -d assignment-service api-gateway
python3 -m pytest tests/test_deadline.py tests/test_resubmit.py tests/test_access.py -v
```

Источники:

- [protobuf](../../libs/proto/tutorflow/assignment.proto);
- [domain service](src/domain/assignment_service.cpp);
- [repository](src/repositories/assignment_repository.cpp);
- [deadline worker](src/workers/deadline_worker.cpp);
- [миграции](../../migrations/assignment/).
