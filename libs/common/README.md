# libs/common

Узкий инфраструктурный каркас, общий для всех сервисов (PLAN §7).
Линкуется как `tutorflow_common`, заголовки — под `tutorflow/common/`.

> **Заморожено (PLAN §16).** Public-сигнатуры меняем только согласованным шагом
> через Lead. Сюда **нельзя** класть DTO и доменные сущности сервисов, а также
> pg-обёртку (userver уже даёт postgres-компоненты).

## Модули

| Заголовок | Назначение |
|---|---|
| `error_codes.hpp` | Машинные коды ошибок (константы), единые для всех. |
| `errors.hpp` | `ServiceError` + `MakeErrorBody` — единый envelope (PLAN §6). |
| `auth_context.hpp` | Парсинг `X-User-Id` / `X-User-Roles`, require-хелперы ролей. |
| `request_context.hpp` | `X-Request-Id` (correlation-id), задел под трейсинг. |
| `http_client_base.hpp` | Базовый клиент к другим сервисам: base URL, проброс `X-User-*`/`X-Request-Id`, разбор error-envelope в `ServiceError`. |
| `health_handler.hpp` | Общий `GET /health -> {"status":"ok"}` (компонент `health-handler`). |

## Использование

`main.cpp` сервиса:

```cpp
#include <tutorflow/common/health_handler.hpp>

auto list = userver::components::MinimalServerComponentList()
    .Append<tutorflow::common::HealthHandler>();
```

`static_config.yaml`:

```yaml
components_manager:
    components:
        health-handler:
            path: /health
            method: GET
            task_processor: main-task-processor
```

Ошибки в handler-коде:

```cpp
if (!ctx.IsTeacher()) throw tutorflow::common::ServiceError::Forbidden();
```

Клиент к чужому сервису наследует `HttpClientBase` и добавляет типобезопасные
методы; транспорт и разбор ошибок — в базе.
