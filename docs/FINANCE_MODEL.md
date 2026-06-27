# TutorFlow — Финансовая модель

finance-service — единственный владелец денег. Реальных платежей нет: это
**append-only журнал операций** + чеки с ручным подтверждением. Главный принцип:
операции **не редактируем и не удаляем** — любое изменение состояния выражается
добавлением новой операции.

## Данные

```sql
financial_transactions(
  id, teacher_id, student_id, type, amount, currency,
  lesson_id?, receipt_id?, comment, created_at)

payment_receipts(
  id, teacher_id, student_id, file_id, amount, currency,
  status, submitted_at, reviewed_at, reviewed_by, comment)
```

`type`: `charge | payment | correction | refund`. Статус чека:
`pending_review | confirmed | rejected`.

## Баланс

Баланс ученика (его долг преподавателю) — сумма по журналу:

```
balance = Σ charge − Σ payment + Σ correction − Σ refund
```

`balance > 0` — ученик должен; `= 0` — долгов нет; `< 0` — переплата/кредит.
Кредит — допустимое состояние (например, отменили уже оплаченное занятие).

## Потоки

**Начисление за занятие.** При `complete` lesson-service пишет статус + `lesson.completed`
в outbox одной транзакцией. finance-consumer создаёт `charge(amount = lessons.price)`.
Charge создаётся ТОЛЬКО из события — gateway и lesson напрямую charge не создают.
**Один charge на занятие** за всё время.

**Оплата (ручная, в два шага).** Ученик загружает файл-чек → `payment_receipt(pending_review)`.
**Баланс при загрузке НЕ меняется.** Преподаватель подтверждает → finance создаёт
`payment(amount = receipt.amount)`, баланс уменьшается. Отклонение → `rejected`, payment
не создаётся. Чек привязан к ученику, не к конкретному начислению (оплата может быть
наперёд/частичной).

**Отмена завершённого занятия.** lesson эмитит `lesson.cancelled(previous_status=completed,
price)`. finance добавляет компенсирующую `correction(−price)`. Charge при этом НЕ удаляется
(append-only) — вклад занятия в баланс становится 0.

**Восстановление занятия.** Если отменённое занятие было завершено (`completed_at`
установлен), reactivate возвращает его в `completed` и эмитит `lesson.restored(price)`.
finance добавляет `correction(+price)` — долг возвращается. По циклу
завершить → отменить → восстановить вклад занятия: `+price → 0 → +price`. Charge остаётся
один; cancel/restore выражаются парами корректировок.

**Ручная коррекция.** Преподаватель через `CreateCorrection(student, amount ±, comment)`
добавляет `correction` (после identity check-access). Универсальный инструмент под
нестандартные ситуации. `amount == 0 → 422`; `comment` обязателен.

## Идемпотентность (два паттерна)

- **charge** (`lesson.completed`): частичный `unique(lesson_id)` на `type='charge'` + inbox
  `processed_events`. Повтор complete / replay события второй charge не создают.
- **корректировки** (`lesson.cancelled` / `lesson.restored`): **атомарный event-id inbox** —
  вставка `correction` + `balance.changed` в outbox + отметка `processed_events(event_id)`
  одним statement; correction пишется, только если событие ещё не обработано
  (`INSERT ... ON CONFLICT(event_id) DO NOTHING RETURNING`). Это позволяет нескольким
  корректировкам на одно занятие (пары ±price) и переживает падение между шагами.
  (Поэтому `unique(lesson_id)` для корректировок снят миграцией — гард перенесён на inbox.)

Чек: подтверждение/отклонение идемпотентны — повтор того же действия возвращает текущее
состояние без второго `payment`; смена финального решения (confirm↔reject) → `409`.

## Доступ

`GET .../balance` и `.../transactions` доступны только самому ученику
(`X-User-Id == student`) или его преподавателю (identity check-access); иначе `403`.

## balance.changed и report-service

После любой меняющей баланс операции finance публикует `balance.changed` с **абсолютным**
`balance_amount` (баланс ПОСЛЕ операции, посчитанный finance в той же транзакции; плюс
`delta` для аудита). report-service хранит это готовое значение и **сам деньги не считает** —
дашборд показывает `debt_amount = max(balance,0)`, `overpaid_amount = max(−balance,0)`,
чеки на проверке отдельно (`pending_*`, не уменьшают долг до подтверждения). Teacher-агрегат
считается по `Σ debt_amount`, а не по `Σ balance`. report — read-model, не source of truth;
истина по деньгам всегда в finance ledger.

## Инварианты (резюме)

1. Журнал append-only: правок/удалений нет, только новые операции.
2. Один `charge` на занятие; откат — компенсирующими `correction`.
3. Загрузка чека баланс не меняет; меняет только подтверждение преподавателем.
4. Идемпотентность: повтор/replay не двоит charge/payment/correction.
5. finance — source of truth по балансу; report лишь зеркалит готовое значение.

> Полный дизайн жизненного цикла занятия и корректировок — `docs/agent-lesson-lifecycle.md`;
> read-model дашбордов — `docs/agent-report-service.md`; контракты событий — `docs/EVENTS.md`.
