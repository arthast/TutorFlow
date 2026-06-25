# 5L: финансовая часть жизненного цикла занятия — компенсация отмены
# завершённого занятия (append-only) и ручная коррекция баланса.
from tests import _client as api


def money(value):
    return round(float(value), 2)


def complete_lesson(teacher, lesson):
    return api.post(f"/lessons/{lesson['id']}/complete", token=teacher["token"], body={})


def corrections_for(student, lesson_id):
    return [
        tx for tx in api.transactions(student)
        if tx["type"] == "correction" and tx.get("lesson_id") == lesson_id
    ]


def charges_for(student, lesson_id):
    return [
        tx for tx in api.transactions(student)
        if tx["type"] == "charge" and tx.get("lesson_id") == lesson_id
    ]


def test_cancel_completed_reverses_charge_append_only(teacher, student, lesson):
    # complete -> charge (+1000), баланс 1000
    status, body = complete_lesson(teacher, lesson)
    assert status == 200, body
    api.wait_for_lesson_charge(student, lesson["id"])
    assert money(api.balance(student)) == 1000.0

    # cancel завершённого -> компенсация correction(-1000), eventual
    status, cancelled = api.post(
        f"/lessons/{lesson['id']}/cancel", token=teacher["token"], body={}
    )
    assert status == 200, cancelled
    assert cancelled["status"] == "cancelled"

    corrections = api.wait_for_lesson_correction(student, lesson["id"])
    assert money(corrections[0]["amount"]) == -1000.0

    # charge НЕ удалён (append-only): в журнале и charge, и correction
    charges = charges_for(student, lesson["id"])
    assert len(charges) == 1, charges
    # вклад занятия в баланс возвращается к 0
    assert money(api.balance(student)) == 0.0


def test_cancel_completed_idempotent_no_double_correction(teacher, student, lesson):
    complete_lesson(teacher, lesson)
    api.wait_for_lesson_charge(student, lesson["id"])

    status, _ = api.post(f"/lessons/{lesson['id']}/cancel", token=teacher["token"], body={})
    assert status == 200
    api.wait_for_lesson_correction(student, lesson["id"])

    # повторная отмена уже cancelled -> 200 текущего, второй correction НЕ создаётся
    status, repeated = api.post(
        f"/lessons/{lesson['id']}/cancel", token=teacher["token"], body={}
    )
    assert status == 200, repeated
    assert repeated["status"] == "cancelled"

    assert len(corrections_for(student, lesson["id"])) == 1
    assert money(api.balance(student)) == 0.0


def test_cancel_scheduled_creates_no_correction(teacher, student, lesson):
    # отмена scheduled-занятия (charge не было) -> компенсации нет, баланс 0
    status, cancelled = api.post(
        f"/lessons/{lesson['id']}/cancel", token=teacher["token"], body={}
    )
    assert status == 200, cancelled
    assert cancelled["status"] == "cancelled"
    assert corrections_for(student, lesson["id"]) == []
    assert money(api.balance(student)) == 0.0


def test_manual_correction_changes_balance(teacher, student):
    assert money(api.balance(student)) == 0.0
    status, tx = api.post(
        f"/students/{student['user_id']}/corrections",
        token=teacher["token"],
        body={"amount": 250, "comment": "manual adjustment"},
    )
    assert status == 201, tx
    assert tx["type"] == "correction"
    assert money(tx["amount"]) == 250.0
    assert tx.get("lesson_id") in (None, "")
    assert money(api.balance(student)) == 250.0

    # отрицательная коррекция тоже допустима (кредит)
    status, tx2 = api.post(
        f"/students/{student['user_id']}/corrections",
        token=teacher["token"],
        body={"amount": -100, "comment": "partial credit"},
    )
    assert status == 201, tx2
    assert money(api.balance(student)) == 150.0


def test_manual_correction_foreign_student_forbidden(teacher, student):
    other_teacher = api.register_teacher()
    status, body = api.post(
        f"/students/{student['user_id']}/corrections",
        token=other_teacher["token"],
        body={"amount": 100, "comment": "should be forbidden"},
    )
    assert status == 403, body


def test_manual_correction_zero_amount_rejected(teacher, student):
    status, body = api.post(
        f"/students/{student['user_id']}/corrections",
        token=teacher["token"],
        body={"amount": 0, "comment": "zero is invalid"},
    )
    assert status == 422, body
