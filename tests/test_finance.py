from tests import _client as api


def money(value):
    return round(float(value), 2)


def complete_lesson(teacher, lesson):
    return api.post(f"/lessons/{lesson['id']}/complete", token=teacher["token"], body={})


def create_receipt(student, amount=400):
    file_id = api.upload_receipt_file(student["token"])
    status, receipt = api.post("/payments/receipts", token=student["token"], body={
        "teacher_id": student["teacher_id"],
        "file_id": file_id,
        "amount": amount,
        "currency": "RUB",
        "comment": "payment test receipt",
    })
    assert status == 201, receipt
    assert receipt["status"] == "pending_review"
    return receipt


def test_complete_is_idempotent_for_charge(teacher, student, lesson):
    status, body = complete_lesson(teacher, lesson)
    assert status == 200, body
    assert money(api.balance(student)) == 1000.0

    status, body = complete_lesson(teacher, lesson)
    assert status == 200, body
    assert money(api.balance(student)) == 1000.0

    charges = [
        tx for tx in api.transactions(student)
        if tx["type"] == "charge" and tx.get("lesson_id") == lesson["id"]
    ]
    assert len(charges) == 1, charges


def test_receipt_balance_rules(teacher, student, lesson):
    status, body = complete_lesson(teacher, lesson)
    assert status == 200, body
    assert money(api.balance(student)) == 1000.0

    receipt = create_receipt(student)
    assert money(api.balance(student)) == 1000.0

    status, confirmed = api.post(
        f"/payments/receipts/{receipt['id']}/confirm",
        token=teacher["token"],
        body={},
    )
    assert status == 200, confirmed
    assert confirmed["status"] == "confirmed"
    assert money(api.balance(student)) == 600.0

    status, confirmed_again = api.post(
        f"/payments/receipts/{receipt['id']}/confirm",
        token=teacher["token"],
        body={},
    )
    assert status == 200, confirmed_again
    assert confirmed_again["status"] == "confirmed"
    assert money(api.balance(student)) == 600.0

    payments = [
        tx for tx in api.transactions(student)
        if tx["type"] == "payment" and tx.get("receipt_id") == receipt["id"]
    ]
    assert len(payments) == 1, payments
    assert money(payments[0]["amount"]) == 400.0

    status, body = api.post(
        f"/payments/receipts/{receipt['id']}/reject",
        token=teacher["token"],
        body={"comment": "cannot change final decision"},
    )
    assert status == 409, body

    rejected_receipt = create_receipt(student, amount=200)

    status, rejected = api.post(
        f"/payments/receipts/{rejected_receipt['id']}/reject",
        token=teacher["token"],
        body={"comment": "bad receipt"},
    )
    assert status == 200, rejected
    assert rejected["status"] == "rejected"
    assert money(api.balance(student)) == 600.0

    status, body = api.post(
        f"/payments/receipts/{rejected_receipt['id']}/confirm",
        token=teacher["token"],
        body={},
    )
    assert status == 409, body

    rejected_payments = [
        tx for tx in api.transactions(student)
        if tx["type"] == "payment" and tx.get("receipt_id") == rejected_receipt["id"]
    ]
    assert rejected_payments == []
