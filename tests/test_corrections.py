import os
import subprocess
import time
from pathlib import Path

# 5L: финансовая часть жизненного цикла занятия — компенсация отмены/
# восстановления завершённого занятия (append-only) и ручная коррекция баланса.
from tests import _client as api


REPO_ROOT = Path(__file__).resolve().parents[1]


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


def replay_lesson_event(lesson_id, event_type):
    sql = f"""
      SELECT jsonb_build_object(
        'event_id', id::text,
        'event_type', event_type,
        'event_version', event_version,
        'occurred_at', to_char(created_at AT TIME ZONE 'UTC',
                               'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
        'producer', 'lesson-service',
        'payload', payload
      )::text
      FROM outbox_events
      WHERE aggregate_id = '{lesson_id}'::uuid
        AND event_type = '{event_type}'
      ORDER BY created_at DESC, id DESC
      LIMIT 1
    """
    user = os.environ.get("POSTGRES_USER", "tutorflow")
    result = subprocess.run(
        [
            "docker",
            "compose",
            "exec",
            "-T",
            "postgres",
            "psql",
            "-U",
            user,
            "-d",
            "lesson_db",
            "-Atc",
            sql,
        ],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    envelope = result.stdout.strip()
    assert envelope, (lesson_id, event_type, result.stderr)
    subprocess.run(
        [
            "docker",
            "compose",
            "exec",
            "-T",
            "kafka",
            "/opt/kafka/bin/kafka-console-producer.sh",
            "--bootstrap-server",
            "localhost:9092",
            "--topic",
            f"tutorflow.{event_type}",
            "--property",
            "parse.key=true",
            "--property",
            "key.separator=:",
        ],
        cwd=REPO_ROOT,
        input=f"{lesson_id}:{envelope}\n",
        check=True,
        text=True,
    )


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


def test_reactivate_completed_restores_debt_and_replay_is_idempotent(
    teacher, student, lesson
):
    complete_lesson(teacher, lesson)
    api.wait_for_lesson_charge(student, lesson["id"])

    status, cancelled = api.post(
        f"/lessons/{lesson['id']}/cancel", token=teacher["token"], body={}
    )
    assert status == 200, cancelled
    api.wait_for_lesson_correction(student, lesson["id"])
    assert money(api.balance(student)) == 0.0

    status, restored = api.post(
        f"/lessons/{lesson['id']}/reactivate", token=teacher["token"], body={}
    )
    assert status == 200, restored
    assert restored["status"] == "completed"

    api.wait_for_lesson_corrections(
        student,
        lesson["id"],
        amounts=[-api.LESSON_PRICE, api.LESSON_PRICE],
        expected_balance=api.LESSON_PRICE,
    )
    assert len(charges_for(student, lesson["id"])) == 1

    replay_lesson_event(lesson["id"], "lesson.cancelled")
    replay_lesson_event(lesson["id"], "lesson.restored")
    time.sleep(2.0)

    corrections = corrections_for(student, lesson["id"])
    assert sorted(money(tx["amount"]) for tx in corrections) == [
        -api.LESSON_PRICE,
        api.LESSON_PRICE,
    ]
    assert len(charges_for(student, lesson["id"])) == 1
    assert money(api.balance(student)) == api.LESSON_PRICE


def test_cancel_scheduled_creates_no_correction(teacher, student, lesson):
    # отмена scheduled-занятия (charge не было) -> компенсации нет, баланс 0
    status, cancelled = api.post(
        f"/lessons/{lesson['id']}/cancel", token=teacher["token"], body={}
    )
    assert status == 200, cancelled
    assert cancelled["status"] == "cancelled"
    assert corrections_for(student, lesson["id"]) == []
    assert money(api.balance(student)) == 0.0

    status, reactivated = api.post(
        f"/lessons/{lesson['id']}/reactivate", token=teacher["token"], body={}
    )
    assert status == 200, reactivated
    assert reactivated["status"] == "scheduled"
    time.sleep(2.0)
    assert charges_for(student, lesson["id"]) == []
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
