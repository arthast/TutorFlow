# Этап 5E: transactional outbox + lesson.completed → finance charge

Первый реальный Kafka-flow.

- 5E-1: стратегия **C** — прямой вызов `lesson → finance` был оставлен
  параллельно с outbox/Kafka, consumer дедупил по `unique(lesson_id)`.
- 5E-2: cutover в вариант **A** — прямой вызов убран. `lesson.completed` из
  outbox — единственный продуктовый путь создания charge.

## 1. Outbox-таблица (lesson_db)

`migrations/lesson/003_outbox_events.sql`:
```
outbox_events(id uuid pk, aggregate_type, aggregate_id uuid, event_type,
              event_version int, payload jsonb, status 'pending'|'published',
              created_at, published_at)
```
+ частичный индекс `idx_outbox_pending (created_at) WHERE status='pending'`.

## 2. Запись в одной транзакции (`LessonRepository::CompleteLesson`)

Смена статуса и вставка outbox-строки — один CTE-стейтмент (= одна транзакция):
```sql
WITH completed AS (
  UPDATE lessons SET status='completed', completed_at=now()
  WHERE id=$1 AND teacher_id=$2 AND status='scheduled' RETURNING *
), outbox AS (
  INSERT INTO outbox_events(aggregate_type, aggregate_id, event_type, event_version, payload)
  SELECT 'lesson', id, 'lesson.completed', 1,
         jsonb_build_object('lesson_id', id::text, 'teacher_id', teacher_id::text,
           'student_id', student_id::text, 'price', price::double precision,
           'currency', 'RUB', 'completed_at', to_char(completed_at ...))
  FROM completed
) SELECT <lesson fields> FROM completed
```
Если занятие уже `completed`, доменный early-return срабатывает **до** апдейта —
повторный complete НЕ пишет второй outbox-строки. payload самодостаточен
(`docs/event-contracts/lesson.completed.v1.json`): finance не ходит обратно в lesson.

## 3. Outbox publisher (lesson-service)

Компонент `lesson-outbox-publisher` (`utils::PeriodicTask`, 1s):
1. `SELECT ... FROM outbox_events WHERE status='pending' ORDER BY created_at LIMIT 100`.
2. На каждую строку: собрать `EventEnvelope` (event_id = outbox.id — стабильный
   между ретраями, occurred_at = created_at, producer = lesson-service, payload) →
   `EventPublisher.Publish("tutorflow.lesson.completed", key=aggregate_id, env)`.
3. `UPDATE status='published', published_at=now() WHERE id=$1 AND status='pending'`.

**Publish-then-mark = at-least-once.** Падение между шагами 2 и 3 → строка остаётся
pending → переотправится на след. тике (дубликат события; консьюмер дедупит).
Один инстанс lesson-service + неперекрывающийся `PeriodicTask` ⇒ нет конкурентной
публикации (для нескольких инстансов понадобился бы `FOR UPDATE SKIP LOCKED` в
транзакции; намеренно не усложняем — Kafka-publish внутри открытой БД-транзакции
держать не хотим).

Конфиг: `kafka-producer` (secdist brokers `kafka:9092`, PLAINTEXT,
`enable_idempotence`), task-processor `producer-task-processor`.

## 4. finance consumer

Компонент `finance-lesson-completed-consumer` (`EventConsumer` на топик
`tutorflow.lesson.completed`, group `finance-lesson-completed`,
`auto_offset_reset: smallest`):
- парсит envelope → payload → `FinanceService.CreateCharge(...)`.
- **Идемпотентность по `lesson_id`**: репозиторий делает
  `INSERT ... ON CONFLICT (lesson_id) WHERE type='charge' DO NOTHING`. Дубликат
  события → `created=false`, второй charge НЕ создаётся.

Конфиг: `kafka-consumer` (secdist brokers), task-processor'ы
`consumer-task-processor` + `consumer-blocking-task-processor`.

## 5. Cutover 5E-2

В `lesson-service` удалены:

- `HttpFinanceClient`;
- прямой `POST /internal/charges` из `LessonService::CompleteLesson`;
- `finance-client` config и HTTP client plumbing, оставшиеся без потребителей.

В `finance-service` снят REST handler `/internal/charges`. Доменный
`FinanceService::CreateCharge` оставлен: его вызывает consumer
`lesson.completed`, а также существующий gRPC контракт finance не менялся в 5E-2.

`CompleteLesson` теперь возвращает:

```json
{"lesson": {"status": "completed"}, "charge_status": "pending"}
```

`charge_status="pending"` означает, что lesson уже завершён и outbox-событие
записано атомарно с изменением статуса; charge станет видимым после публикации
события и обработки finance consumer.

Frontend после complete читает `charge_status` и запускает короткий refetch/poll
баланса ученика. Smoke и pytest обновлены на eventual consistency: они ждут
появления charge/баланса с таймаутом.

## 6. Почему нет двойного charge

Повторный complete для уже `completed` занятия возвращает текущий lesson и не
пишет вторую outbox-строку. Повторная доставка Kafka-события безопасна:
`financial_transactions` имеет idempotency по `lesson_id`, поэтому второй insert
даёт `ON CONFLICT DO NOTHING`.

## Как проверить

```sh
docker compose up -d                 # kafka healthy; lesson/finance ждут kafka
./scripts/migrate.sh all             # применит 003_outbox_events.sql
python3 scripts/smoke_mvp.py         # SMOKE OK (complete внутри сценария)

# ровно один charge на занятие:
docker compose exec postgres psql -U "$POSTGRES_USER" -d finance_db -c \
  "SELECT lesson_id, count(*) FROM financial_transactions \
   WHERE type='charge' GROUP BY lesson_id HAVING count(*) > 1;"   # -> 0 строк

# outbox опубликован:
docker compose exec postgres psql -U "$POSTGRES_USER" -d lesson_db -c \
  "SELECT status, count(*) FROM outbox_events GROUP BY status;"

# лог консьюмера:
docker compose logs finance-service | grep 'lesson.completed'
```

## Что НЕ делалось

- Proto/OpenAPI контракты не менялись в этой задаче; они были обновлены
  координатором заранее.
- Другие события (`assignment.*`, `payment.*`) — 5F.
- Остальные gRPC-методы и бизнес-flow не тронуты.
