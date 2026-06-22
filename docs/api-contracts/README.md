# API-контракты TutorFlow

Источник правды по эндпоинтам. OpenAPI 3.0.3, по файлу на сервис.
Меняются только согласованно (через Lead, см. AGENTS.md «Изменение контрактов»).

| Файл | Сервис | Доступ | Owner |
|---|---|---|---|
| `gateway.openapi.yaml` | api-gateway | внешний (Bearer JWT) | Agent A |
| `identity.openapi.yaml` | identity-service | internal (`/internal/*`) | Agent A |
| `file.openapi.yaml` | file-service | internal | Agent A |
| `lesson.openapi.yaml` | lesson-service | internal | Agent B |
| `assignment.openapi.yaml` | assignment-service | internal | Agent B |
| `finance.openapi.yaml` | finance-service | internal | Agent B |

## Соглашения
- Внешние запросы: `gateway` с `Authorization: Bearer <JWT>`.
- Внутренние: `/internal/*`, заголовки `X-User-Id` / `X-User-Roles` от gateway
  (gateway срезает клиентские и проставляет заново). Сервисы JWT не валидируют.
- Ошибки — единый envelope `{ "error": { code, message, details } }`.
- Деньги: `amount` (number) + `currency`. Время: `date-time`. ID: `uuid`.

## Ключевые инварианты (сверено с PLAN §8/§10)
- Связь teacher↔student проверяется только через `identity check-access`.
- Файлы только через file-service; в других сервисах — `file_id`.
- `charge` создаёт lesson-service при `complete`, идемпотентно `unique(lesson_id)`.
- Баланс = `sum(charge) - sum(payment) + sum(correction) - sum(refund)`;
  меняется только после подтверждения чека (не при загрузке).
- Идемпотентность: complete→charge, confirm→payment.

## Для Agent B (Codex)
До готовности identity/file пиши против `identity.openapi.yaml` и `file.openapi.yaml`
через client-interface + stub/mock, потом переключай на реальный HTTP-клиент.

## Проверка
```bash
# синтаксис YAML
python3 -c "import yaml,glob;[yaml.safe_load(open(f)) for f in glob.glob('docs/api-contracts/*.yaml')]"
# при желании — линт OpenAPI (если установлен redocly/swagger-cli)
```
