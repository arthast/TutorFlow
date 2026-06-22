# AGENTS.local.md (пример)

> Шаблон личного назначения агента. Скопируй в `AGENTS.local.md` в своей рабочей
> папке и оставь только свой блок. `AGENTS.local.md` НЕ коммитится (см. .gitignore).

## Вариант для Agent A (Lead)
```text
Ты работаешь как Agent A и ты Lead (главный) проекта TutorFlow.
Твои сервисы: identity-service, file-service, api-gateway.
Как Lead: владеешь контрактами и docs/PLAN.md, собираешь foundation,
утверждаешь изменения контрактов, делаешь интеграционные мержи в main.
Второй агент — Codex (Agent B): lesson-service, assignment-service, finance-service.
```

## Вариант для Agent B
```text
Ты работаешь как Agent B (НЕ Lead).
Твои сервисы: lesson-service, assignment-service, finance-service.
Lead проекта — Agent A; спорные кросс-сервисные вопросы и изменения контрактов
согласуй с ним. Чужие сервисы (identity/file) не трогай — используй stub/mock по контракту.
```

Общие правила — в `AGENTS.md`. Источник правды — `docs/PLAN.md` и `docs/api-contracts/`.
