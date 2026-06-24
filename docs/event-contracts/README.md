# Event contracts (Этап 5D/5F)

Versioned JSON contracts for TutorFlow domain events. **Описания, не код** —
исходник envelope/сериализации живёт в `libs/events`.

## Envelope (общий для всех событий)

```json
{
  "event_id":      "uuid v4 — уникальный id события (идемпотентность на 5E)",
  "event_type":    "тип, напр. lesson.completed",
  "event_version": 1,
  "occurred_at":   "ISO-8601 UTC, напр. 2026-06-24T10:00:00Z",
  "producer":      "сервис-источник, напр. lesson-service",
  "trace_id":      "корреляция запроса (может быть пустым)",
  "payload":       { "...event-type specific, самодостаточный..." }
}
```

## Топики и имена событий

Неймспейс: `tutorflow.<domain>.<event>`, напр. `tutorflow.lesson.completed`.
Ключ сообщения (key) — id агрегата (lesson_id / assignment_id / receipt_id) для
покопартиционной упорядоченности.

Event type пишется в форме `<domain>.<past_tense>`: `assignment.created`,
`submission.uploaded`, `payment.confirmed`, `balance.changed`. Kafka используется
только для фактов, которые уже произошли; команды через Kafka не отправляем.

## Правила

- **payload самодостаточен**: консьюмер не должен ходить обратно к продюсеру за
  данными (напр. finance из `lesson.completed` берёт `price`, не зовёт lesson).
- Версионирование — в имени файла (`*.v1.json`) и поле `event_version`.
  Несовместимые изменения → новая версия (`*.v2.json`), старая поддерживается.
- Формат — JSON (не Protobuf) по решению roadmap (Этап 5D).
- Доставка — at-least-once. Consumer должен быть идемпотентным: через доменный
  unique constraint и/или inbox `processed_events(event_id primary key)`.
- Retry/DLQ convention фиксируется на уровне consumer'ов: безопасные повторы
  допустимы, poison message должен попадать в отдельный DLQ topic при появлении
  production-grade consumer loop.

Файлы:
- `lesson.completed.v1.json`
- `assignment.created.v1.json`
- `submission.uploaded.v1.json`
- `assignment.reviewed.v1.json`
- `payment_receipt.uploaded.v1.json`
- `payment.confirmed.v1.json`
- `payment.rejected.v1.json`
- `charge.created.v1.json`
- `balance.changed.v1.json`
