# report-service

`report-service` строит read-models для teacher/student dashboards. Он собирает
нужные UI агрегаты заранее, чтобы gateway не выполнял fan-out по всем доменным
сервисам при каждом открытии страницы.

Это projection, а не источник истины. Истина по занятиям остаётся в lesson,
по заданиям — в assignment, по деньгам — в finance.

[Вернуться к общей архитектуре](../../README.md)

## gRPC API

Контракт: [`report.proto`](../../libs/proto/tutorflow/report.proto).

| RPC | Назначение |
|---|---|
| `GetTeacherDashboard` | общие показатели и список student summaries |
| `GetStudentDashboard` | собственные activity/finance показатели ученика |
| `GetStudentSummary` | карточка конкретного ученика для teacher |

Gateway выполняет только простое чтение/маппинг этих моделей.

## Входящие события

| Группа | События |
|---|---|
| Lessons | scheduled, completed, rescheduled, cancelled, restored |
| Assignments | created, submission uploaded, reviewed, deadline expired |
| Receipts | uploaded, confirmed, rejected |
| Finance | `balance.changed` |

`balance.changed` содержит абсолютный `balance_amount`, рассчитанный finance в
той же транзакции, что операция. Report не реализует свою формулу денег.

## Как строится projection

Сервис хранит по одной state-строке на lesson, assignment и receipt, обновляя её
через upsert. Затем агрегаты пары teacher-student пересчитываются через
`COUNT/SUM/MIN/MAX`, а не слепыми `+1/-1`.

Это даёт важные свойства:

- повтор события не увеличивает счётчик второй раз;
- reschedule заменяет время одного lesson;
- review заменяет статус одного assignment;
- итоговые summaries можно восстановить из entity-state таблиц.

`report_processed_events(event_id)` является inbox. Применение event, отметка
inbox и обновление агрегатов выполняются атомарно в `report_db`.

## Eventual consistency

После команды domain service отвечает сразу, а dashboard обновляется после
доставки Kafka event. Короткая задержка допустима.

Если projection расходится с source of truth:

1. доменные сервисы считаются правильными;
2. report DB можно очистить и пересобрать replay событий;
3. автоматическая production-команда полного rebuild пока не реализована.

Поэтому README не обещает мгновенно согласованные dashboard counters.

## Данные

| Таблица | Назначение |
|---|---|
| `report_lessons` | последнее состояние каждого lesson |
| `report_assignments` | последнее состояние каждого assignment |
| `report_receipts` | последнее состояние каждого receipt |
| `student_activity_summary` | activity агрегаты пары |
| `student_finance_summary` | balance/debt/overpayment/receipts |
| `teacher_summary` | агрегаты teacher dashboard |
| `report_processed_events` | consumer inbox |

`debt_amount = max(balance, 0)`, `overpaid_amount = max(-balance, 0)`.
Teacher total debt — сумма долгов, а не алгебраическая сумма balances, чтобы
переплата одного ученика не скрывала долг другого.

## Внутренняя структура

```text
src/main.cpp
  ├── consumers/domain_event_consumer.*  envelope → typed internal model
  ├── repositories/report_repository.*   inbox, upsert, recompute, queries
  ├── domain/report_service.*             access/query orchestration
  ├── grpc/report_grpc_service.*
  └── handlers/ready_handler.*
```

## Runtime и проверка

Нужны `REPORT_DATABASE_URL` и Kafka consumer. Report не публикует новые
доменные события. `/ready` проверяет `report_db`.

```bash
docker compose build report-service
docker compose up -d report-service api-gateway
python3 -m pytest tests/test_report.py -v
```

Источники:

- [protobuf](../../libs/proto/tutorflow/report.proto);
- [consumer](src/consumers/domain_event_consumer.cpp);
- [repository](src/repositories/report_repository.cpp);
- [миграции](../../migrations/report/);
- [report design notes](../../docs/agent-report-service.md).
