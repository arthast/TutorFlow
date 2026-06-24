# Event contracts (Этап 5D)

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

## Топики

Неймспейс: `tutorflow.<domain>.<event>`, напр. `tutorflow.lesson.completed`.
Ключ сообщения (key) — id агрегата (lesson_id / assignment_id / receipt_id) для
покопартиционной упорядоченности.

## Правила

- **payload самодостаточен**: консьюмер не должен ходить обратно к продюсеру за
  данными (напр. finance из `lesson.completed` берёт `price`, не зовёт lesson).
- Версионирование — в имени файла (`*.v1.json`) и поле `event_version`.
  Несовместимые изменения → новая версия (`*.v2.json`), старая поддерживается.
- Формат — JSON (не Protobuf) по решению roadmap (Этап 5D).

Файлы: `lesson.completed.v1.json`, `assignment.created.v1.json`,
`payment.confirmed.v1.json`.
