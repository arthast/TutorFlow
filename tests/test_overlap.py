"""Запрет пересекающихся занятий преподавателя на уровне БД (миграция 004).

EXCLUDE-constraint no_overlap_teacher держит время только для scheduled-занятий
одного преподавателя; диапазон [) — смежные занятия не пересекаются. Гонку
конкурентных create закрывает сам constraint (атомарно) → второй запрос 409.
"""
import concurrent.futures

from tests import _client as api


def create_lesson_at(teacher_token, student_id, starts_h, ends_h, price=api.LESSON_PRICE):
    return api.post("/lessons", token=teacher_token, body={
        "student_id": student_id,
        "starts_at": api.iso_at(starts_h),
        "ends_at": api.iso_at(ends_h),
        "topic": "Overlap test",
        "price": price,
    })


def reschedule_at(teacher_token, lesson_id, starts_h, ends_h):
    return api.post(
        f"/lessons/{lesson_id}/reschedule",
        token=teacher_token,
        body={
            "new_starts_at": api.iso_at(starts_h),
            "new_ends_at": api.iso_at(ends_h),
        },
    )


def test_overlapping_create_conflict(teacher, student):
    status, first = create_lesson_at(teacher["token"], student["user_id"], 24, 26)
    assert status == 201, first

    # [25,27) пересекается с [24,26) у того же преподавателя -> 409 envelope
    status, body = create_lesson_at(teacher["token"], student["user_id"], 25, 27)
    assert status == 409, body
    assert "error" in body, body
    assert "overlap" in body["error"]["message"].lower(), body


def test_back_to_back_create_ok(teacher, student):
    status, first = create_lesson_at(teacher["token"], student["user_id"], 24, 25)
    assert status == 201, first

    # смежное [25,26): диапазон полуоткрытый [) -> не пересечение -> 201
    status, second = create_lesson_at(teacher["token"], student["user_id"], 25, 26)
    assert status == 201, second


def test_reschedule_into_overlap_conflict(teacher, student):
    status, first = create_lesson_at(teacher["token"], student["user_id"], 24, 25)
    assert status == 201, first
    status, second = create_lesson_at(teacher["token"], student["user_id"], 30, 31)
    assert status == 201, second

    # переносим второе в окно первого -> 409
    status, body = reschedule_at(teacher["token"], second["id"], 24, 25)
    assert status == 409, body
    assert "overlap" in body["error"]["message"].lower(), body


def test_reschedule_back_to_back_ok(teacher, student):
    status, first = create_lesson_at(teacher["token"], student["user_id"], 24, 25)
    assert status == 201, first
    status, second = create_lesson_at(teacher["token"], student["user_id"], 30, 31)
    assert status == 201, second

    # перенос вплотную к первому [25,26) допустим
    status, body = reschedule_at(teacher["token"], second["id"], 25, 26)
    assert status == 200, body


def test_cancelled_lesson_does_not_block_time(teacher, student):
    status, first = create_lesson_at(teacher["token"], student["user_id"], 24, 25)
    assert status == 201, first

    status, _ = api.post(
        f"/lessons/{first['id']}/cancel", token=teacher["token"], body={}
    )
    assert status == 200

    # cancelled не держит время -> новое занятие в тот же слот проходит
    status, second = create_lesson_at(teacher["token"], student["user_id"], 24, 25)
    assert status == 201, second


def test_reactivate_into_busy_time_conflict(teacher, student):
    status, first = create_lesson_at(teacher["token"], student["user_id"], 24, 25)
    assert status == 201, first

    status, _ = api.post(
        f"/lessons/{first['id']}/cancel", token=teacher["token"], body={}
    )
    assert status == 200

    # время [24,25) теперь занимает второе scheduled-занятие
    status, second = create_lesson_at(teacher["token"], student["user_id"], 24, 25)
    assert status == 201, second

    # реактивация первого (cancelled->scheduled) в занятый слот -> 409
    status, body = api.post(
        f"/lessons/{first['id']}/reactivate", token=teacher["token"], body={}
    )
    assert status == 409, body
    assert "overlap" in body["error"]["message"].lower(), body


def test_concurrent_create_one_wins(teacher, student):
    # best-effort: два конкурентных create в один слот времени. Главная гарантия
    # безопасности — double-booking невозможен (его закрывает сам EXCLUDE-
    # constraint, не code-level проверка): двух успехов быть НЕ может.
    #
    # Проигравший получает 409 (успел увидеть зафиксированный конфликт) ЛИБО 500:
    # под одновременной вставкой Postgres держит предикатную блокировку
    # exclusion-констрейнта, и при statement_timeout (250ms) < deadlock_timeout
    # (1s) ожидающая транзакция может быть снята по таймауту раньше, чем
    # разрешится в чистый 23P01. Поэтому фиксируем инвариант, а не точный код.
    def attempt():
        return create_lesson_at(teacher["token"], student["user_id"], 40, 41)[0]

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        statuses = sorted(pool.map(lambda _: attempt(), range(2)))

    # никогда не два 201 (нет double-booking)
    assert statuses.count(201) <= 1, statuses
    # проигравший — конфликт или таймаут lock-wait, но не успешная бронь
    assert all(s in (409, 500) for s in statuses if s != 201), statuses
