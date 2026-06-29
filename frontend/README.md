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
- `src/pages/Teacher.tsx` / `src/pages/Student.tsx` — главные dashboard-экраны.
- `src/pages/TeacherLessons.tsx`, `TeacherStudentCard.tsx`,
  `TeacherAssignmentReview.tsx`, `ChatPage.tsx` — страницы по дизайн-референсам.
- `src/pages/TeacherStudents.tsx`, `TeacherAssignments.tsx`,
  `TeacherFinance.tsx`, `TeacherReceipts.tsx`, `TeacherSettings.tsx` — рабочие
  skeleton-страницы без отдельных референсов.
- `src/pages/StudentLessons.tsx`, `StudentAssignments.tsx`,
  `StudentPayments.tsx`, `StudentReceipts.tsx`, `StudentSettings.tsx` — рабочие
  skeleton-страницы ученика без отдельных референсов.

## Routes

```text
/teacher
/teacher/students
/teacher/students/:studentId
/teacher/lessons
/teacher/assignments
/teacher/assignments/:assignmentId/review
/teacher/finance
/teacher/receipts
/teacher/chat
/teacher/settings

/student
/student/lessons
/student/assignments
/student/payments
/student/receipts
/student/chat
/student/settings
```

## Поток оплаты

У ученика **нет баланса/кошелька**: он заявляет сумму и прикладывает чек
(`POST /files` → `POST /payments/receipts`). Баланс (долг) — учётная величина на
стороне преподавателя; он подтверждает чек вручную, и только тогда появляется
операция `payment`.
