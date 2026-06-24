# SETUP — как организовать работу двух агентов

Коммитится в git. Один человек координирует двух агентов:
**Agent A (Lead) — Claude Code**, **Agent B — Codex**.

## Итоговая раскладка папок

```text
~/Documents/programming/cppprojects/
  TutorFlow/            <- Agent A / Lead (identity + file + gateway). Текущая папка.
  TutorFlow-agentB/     <- Agent B / Codex (lesson + assignment + finance). Worktree.
```

Один репозиторий, два рабочих дерева (git worktree), общая история.
`main` — линия интеграции, мерж обеих веток делает Lead.

> Git-политика: в git коммитятся исходники + `docs/` + `AGENTS.md` + `README.md` +
> `.env.example`. Локальны только `.env`, `AGENTS.local.md`, `*.local.md`.
> Поэтому **docs и контракты приходят в worktree автоматически из git** — раздавать
> руками не нужно.

---

## Шаг 0. Проверка репозитория (один раз)
```bash
cd ~/Documents/programming/cppprojects/TutorFlow
git status
git remote -v
```

## Шаг 1. Фундамент в main (делает Agent A = Lead, до разветвления)
Скелет 6 сервисов с /health, docker-compose, libs/common, шаблон CMake/Dockerfile,
migrations/<svc>/001_init.sql + уже готовые docs/ и AGENTS.md. Код пишет Lead,
ты коммитишь и пушишь:
```bash
cd ~/Documents/programming/cppprojects/TutorFlow
git add .
git commit -m "chore: foundation (services skeleton, compose, libs/common, docs, contracts)"
git push origin main
```

## Шаг 2. Ветка Agent A и worktree Agent B
```bash
cd ~/Documents/programming/cppprojects/TutorFlow
git switch -c feat/agentA-identity            # Agent A уходит с main на свою ветку
git worktree add ../TutorFlow-agentB -b feat/agentB-lesson main
```
Теперь:
- `TutorFlow/`        — Agent A (Lead), ветка `feat/agentA-identity`
- `TutorFlow-agentB/` — Agent B (Codex), ветка `feat/agentB-lesson`

> docs/ и AGENTS.md уже в git → в `TutorFlow-agentB/` они появятся сами.

## Шаг 3. Личное назначение и секреты в каждой папке
```bash
# Agent A:
cp AGENTS.local.example.md AGENTS.local.md     # оставить блок "Agent A (Lead)"
cp .env.example .env                           # заполнить секреты
# Agent B:
cd ../TutorFlow-agentB
cp AGENTS.local.example.md AGENTS.local.md     # оставить блок "Agent B"
cp .env.example .env
```

## Шаг 4. Запуск агентов
```bash
# Терминал 1 (Agent A / Lead) — Claude Code:
cd ~/Documents/programming/cppprojects/TutorFlow && claude
# Терминал 2 (Agent B) — Codex:
cd ~/Documents/programming/cppprojects/TutorFlow-agentB && codex
```
Оба инструмента читают `AGENTS.md` (+ `AGENTS.local.md`) из корня папки автоматически.

## Шаг 5. Рабочий цикл (каждый агент)
```bash
git add -A
git commit -m "feat(identity): ..."
git push origin <своя-ветка>
```
Мерж в main делает Lead (через PR на хосте или локально):
```bash
cd ~/Documents/programming/cppprojects/TutorFlow
git fetch origin
git switch main && git merge --no-ff origin/feat/agentB-lesson && git push origin main
git switch feat/agentA-identity
```
После мержа второй агент подтягивает базу:
```bash
git pull --rebase origin main
```
> Изменение контракта — только согласованно с Lead. После мержа контракта оба
> ребейзятся, чтобы видеть одну версию API.

## Шаг 6. Параллельный запуск стека (про порты)
Два полных `docker compose up` на одном хосте конфликтуют по портам (8080–8085, 5432).
- полный стек поднимай в одной папке (обычно Lead/main);
- агент в своей папке тестирует свой сервис изолированно или с отдельным проектом:
  `docker compose -p tutorflow-b up postgres lesson-service`.

## Шаг 7. Завершение
```bash
git worktree remove ../TutorFlow-agentB
git worktree list
```

---

## Частые правила
- Не трогать чужой `services/<svc>/` и `migrations/<svc>/`.
- Контракты/`libs/common` public — менять только через Lead.
- `.env` и `AGENTS.local.md` — локальные, не коммитить.
