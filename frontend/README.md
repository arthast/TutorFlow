# TutorFlow frontend

Минимальный ролевой demo-UI (React + TypeScript + Vite). Ходит **только** в
api-gateway, во внутренние сервисы не обращается.

## Запуск

```bash
cp .env.example .env          # при необходимости поменяй VITE_API_URL
npm install
npm run dev                   # http://localhost:5173
```

Бэкенд должен быть поднят (`docker compose up --build` в корне репо) на
`http://localhost:8080`. Порт фронта 5173 совпадает с `GATEWAY_CORS_ORIGIN` по
умолчанию, так что CORS работает из коробки.

```bash
npm run build                 # tsc + vite build (проверка типов и сборка)
```

## Что внутри

- `src/api.ts` — клиент к gateway: Bearer-токен, разбор единого envelope ошибок,
  типы по `docs/api-contracts/gateway.openapi.yaml`.
- `src/auth.tsx` — контекст авторизации (login/register, `/me`, роль).
- `src/pages/Teacher.tsx` — панель преподавателя: ученики, занятия, ДЗ (проверка
  и комментарии), чеки (подтвердить/отклонить), долг ученика.
- `src/pages/Student.tsx` — панель ученика: занятия, ДЗ (сдать), загрузка чека,
  список своих чеков со статусами, смена пароля.

## Поток оплаты

У ученика **нет баланса/кошелька**: он заявляет сумму и прикладывает чек
(`POST /files` → `POST /payments/receipts`). Баланс (долг) — учётная величина на
стороне преподавателя; он подтверждает чек вручную, и только тогда появляется
операция `payment`.
