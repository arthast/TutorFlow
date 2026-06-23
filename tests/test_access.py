from tests import _client as api


def test_teacher_cannot_work_with_unlinked_student(teacher, student):
    other_teacher = api.register_teacher()

    status, body = api.post("/lessons", token=other_teacher["token"], body={
        "student_id": student["user_id"],
        "starts_at": api.iso_at(24),
        "ends_at": api.iso_at(25),
        "topic": "Forbidden lesson",
        "price": 1000,
    })
    assert status == 403, body

    status, body = api.post("/assignments", token=other_teacher["token"], body={
        "student_id": student["user_id"],
        "title": "Forbidden assignment",
        "description": "Should be denied",
        "file_ids": [],
    })
    assert status == 403, body


def test_student_does_not_see_other_students_assignment(teacher):
    student_one = api.create_student(teacher["token"])
    student_two = api.create_student(teacher["token"])
    assignment = api.create_assignment(
        teacher["token"], student_one["user_id"], "Private assignment"
    )
    assignment_id = assignment["id"]

    status, body = api.get("/assignments", token=student_two["token"])
    assert status == 200, body
    assert assignment_id not in {item["id"] for item in body}

    status, body = api.get(f"/assignments/{assignment_id}", token=student_two["token"])
    assert status == 403, body

    status, body = api.get(f"/assignments/{assignment_id}", token=student_one["token"])
    assert status == 200, body
    assert body["id"] == assignment_id


def test_assignment_role_edges(teacher, student):
    assignment = api.create_assignment(
        teacher["token"], student["user_id"], "Assignment role edges"
    )

    status, body = api.post("/assignments", token=student["token"], body={
        "student_id": student["user_id"],
        "title": "Student-created assignment",
        "description": "Should be denied",
        "file_ids": [],
    })
    assert status == 403, body

    status, body = api.post(
        f"/assignments/{assignment['id']}/review",
        token=student["token"],
        body={"status": "accepted", "comment": "student cannot review"},
    )
    assert status == 403, body

    status, teacher_comment = api.post(
        f"/assignments/{assignment['id']}/comments",
        token=teacher["token"],
        body={"text": "Teacher comment"},
    )
    assert status == 201, teacher_comment
    assert teacher_comment["author_id"] == teacher["user_id"]

    status, student_comment = api.post(
        f"/assignments/{assignment['id']}/comments",
        token=student["token"],
        body={"text": "Student comment"},
    )
    assert status == 201, student_comment
    assert student_comment["author_id"] == student["user_id"]


def test_teacher_cannot_review_foreign_assignment(teacher):
    other_teacher = api.register_teacher()
    other_student = api.create_student(other_teacher["token"])
    assignment = api.create_assignment(
        other_teacher["token"], other_student["user_id"], "Foreign assignment"
    )

    status, body = api.post(
        f"/assignments/{assignment['id']}/review",
        token=teacher["token"],
        body={"status": "accepted", "comment": "foreign review"},
    )
    assert status == 403, body


def test_finance_balance_transactions_access(teacher, student):
    other_teacher = api.register_teacher()
    other_student = api.create_student(other_teacher["token"])

    for endpoint in ("balance", "transactions"):
        # сам ученик читает свои данные
        status, body = api.get(
            f"/students/{student['user_id']}/{endpoint}", token=student["token"]
        )
        assert status == 200, body

        # связанный преподаватель читает данные своего ученика
        status, body = api.get(
            f"/students/{student['user_id']}/{endpoint}", token=teacher["token"]
        )
        assert status == 200, body

        # чужой ученик — запрещено
        status, body = api.get(
            f"/students/{student['user_id']}/{endpoint}",
            token=other_student["token"],
        )
        assert status == 403, body

        # чужой преподаватель — запрещено
        status, body = api.get(
            f"/students/{student['user_id']}/{endpoint}",
            token=other_teacher["token"],
        )
        assert status == 403, body


def test_lesson_role_edges(teacher, student, lesson):
    status, body = api.post("/lessons", token=student["token"], body={
        "student_id": student["user_id"],
        "starts_at": api.iso_at(24),
        "ends_at": api.iso_at(25),
        "topic": "Student-created lesson",
        "price": 1000,
    })
    assert status == 403, body

    status, body = api.post(
        f"/lessons/{lesson['id']}/complete",
        token=student["token"],
        body={},
    )
    assert status == 403, body
    assert api.balance(student) == 0.0

    other_teacher = api.register_teacher()
    other_student = api.create_student(other_teacher["token"])
    foreign_lesson = api.create_lesson(
        other_teacher["token"], other_student["user_id"]
    )

    status, body = api.post(
        f"/lessons/{foreign_lesson['id']}/complete",
        token=teacher["token"],
        body={},
    )
    assert status == 403, body
