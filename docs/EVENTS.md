# TutorFlow — Событийная модель (Kafka)

Kafka используется для **асинхронных доменных фактов** («уже произошло») и побочных
эффектов. Команды и запросы, где нужен ответ сейчас, остаются синхронными (gRPC).
Доставка — at-least-once через transactional **outbox** (producer) и **inbox**
(consumer-идемпотентность).

Полные payload'ы — источник правды в `docs/event-contracts/*.v1.json` (JSON Schema).
Здесь — карта и правила.

## Envelope

```json
{
  "event_id": "uuid", "event_type": "lesson.completed", "event_version": 1,
  "occurred_at": "RFC3339", "producer": "lesson-service", "trace_id": "...",
  "payload": { ... }
}
```

Топик: `tutorflow.<event_type>`. Ключ партиции — по агрегату (`lesson_id`,
`student_id`, `receipt_id`, `dialog_id`), чтобы события одной сущности шли по порядку.
Нейминг: `<domain>.<past_tense>` (`lesson.completed`, `payment.confirmed`, `message.sent`).

## Outbox / Inbox

- **Producer**: пишет событие в таблицу `outbox_events` ТОЙ ЖЕ транзакцией, что и
  изменение состояния (атомарность факта и события). Отдельный publisher (PeriodicTask)
  отправляет в Kafka и помечает строку `published`.
- **Consumer**: дедуплицирует по `processed_events(event_id)` (inbox), чтобы replay /
  повторная доставка не давали двойного эффекта.

## Паттерны идемпотентности (три)

1. **Бизнес-ключ + inbox** — finance `charge` (частичный `unique(lesson_id)`),
   `payment` (`unique(receipt_id)`). На конфликте возвращается существующая строка
   (`created=false`), без второго `balance.changed`.
2. **Атомарный event-id inbox** — finance корректировки (`lesson.cancelled`/`restored`):
   вставка операции + `balance.changed` + `processed_events(event_id)` одним statement;
   пишется только если событие ещё не обработано. Допускает несколько операций на занятие.
3. **Состояние-на-сущность + inbox** — report-service: строка на занятие/ДЗ/чек (upsert),
   агрегаты пересчитываются `SUM/COUNT` (а не инкрементами) → устойчиво к replay;
   notification: inbox + `unique(user_id, source_event_id)`.

## Каталог событий (15)

| Событие | Producer | Consumers | Назначение |
|---|---|---|---|
| `lesson.scheduled` | lesson | notification, report | занятие назначено (origin=created) / восстановлено (reactivated) |
| `lesson.completed` | lesson | **finance → charge**, notification, report | занятие проведено |
| `lesson.rescheduled` | lesson | notification, report | перенос времени |
| `lesson.cancelled` | lesson | **finance → correction(−)**, notification, report | отмена (компенсация если было completed) |
| `lesson.restored` | lesson | **finance → correction(+)**, notification, report | восстановление завершённого, возврат долга |
| `assignment.created` | assignment | notification, report | выдано ДЗ |
| `submission.uploaded` | assignment | notification, report | ученик сдал решение |
| `assignment.reviewed` | assignment | notification, report | проверено |
| `charge.created` | finance | report | начисление (read-model/audit) |
| `payment_receipt.uploaded` | finance | notification, report | чек загружен |
| `payment.confirmed` | finance | notification, report | оплата подтверждена |
| `payment.rejected` | finance | notification, report | чек отклонён |
| `balance.changed` | finance | **report** (абс. balance_amount), notification (reason=correction.created) | баланс изменился |
| `message.sent` | chat | notification (recipient) | новое сообщение в диалоге |
| `message.read` | chat | — (задел под read-models) | сообщения прочитаны |

Полужирным — потребители с бизнес-эффектом. `balance.changed` несёт **абсолютный**
`balance_amount` (см. `docs/FINANCE_MODEL.md`), report хранит готовое значение и сам
деньги не считает.

## Source of truth

События — это факты постфактум, а не источник истины. Истина остаётся в доменных
сервисах (lesson/assignment/finance/identity). Read-models (report) и уведомления
(notification) — производные; при расхождении правда в доменном сервисе, read-model
пересобираем из событий.

## Отложенные события

`assignment.deadline_expired` (нужен deadline-worker), identity-события
(`user.registered`, `student.created`, `teacher_student_link.created`,
`password.changed`), `file.uploaded/deleted` — добавлять только под конкретного
потребителя. Реалтайм-эффекты чата (`message.read` → unread/read-models) — позже.
