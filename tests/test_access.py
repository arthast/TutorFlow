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
