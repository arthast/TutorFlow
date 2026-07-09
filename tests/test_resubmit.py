from tests import _client as api


def create_submissions(assignment_id, student_token):
    status, sub1 = api.post(
        f"/assignments/{assignment_id}/submit",
        token=student_token,
        body={"text_answer": "готово", "file_ids": []},
    )

    assert status == 201, (status, sub1)

    assert sub1["text_answer"] == 'готово'

    status, sub2 = api.post(
        f"/assignments/{assignment_id}/submit",
        token=student_token,
        body={"text_answer": "готово2", "file_ids": []},
    )

    assert status == 201, (status, sub2)

    assert sub2["text_answer"] == 'готово2'

    return (sub1, sub2)


def test_resubmit_assignment_unchecked(teacher, student):
    assignment = api.create_assignment(teacher["token"], student["user_id"])

    sub1, sub2 = create_submissions(assignment['id'], student["token"])

    status, assgn = api.get(f"/assignments/{assignment['id']}", token=student["token"])

    assert status == 200, (status, assgn)

    assert assgn["status"] == "submitted" and len(assgn["submissions"]) == 2, assgn

    status, assgn = api.get(f"/assignments/{assignment['id']}", token=teacher["token"])

    assert status == 200, (status, assgn)

    assert assgn["status"] == "submitted" and len(assgn["submissions"]) == 2, assgn


def test_assignment_rechecked(teacher, student):
    assignment = api.create_assignment(teacher["token"], student["user_id"])

    sub1, sub2 = create_submissions(assignment['id'], student["token"])

    status, review = api.post(
        f"/assignments/{assignment['id']}/review",
        token=teacher["token"],
        body={"status": "needs_fix", "comment": "fix please"},
    )
    assert status == 200, review

    assert review["status"] == "needs_fix" and review["text_answer"] == "готово2", review

    status, review = api.post(
        f"/assignments/{assignment['id']}/review",
        token=teacher["token"],
        body={"status": "needs_fix", "comment": "fix please2"},
    )

    assert status == 200, review

    assert review["status"] == "needs_fix" and review["text_answer"] == "готово2", review

    status, review = api.post(
        f"/assignments/{assignment['id']}/review",
        token=teacher["token"],
        body={"status": "accepted", "comment": "good job"},
    )

    assert status == 200, review

    assert review["status"] == "accepted" and review["text_answer"] == "готово2", review


def test_resubmit_conflict(teacher, student):
    assignment = api.create_assignment(teacher["token"], student["user_id"])

    status, sub = api.post(
        f"/assignments/{assignment['id']}/submit",
        token=student["token"],
        body={"text_answer": "готово", "file_ids": []},
    )

    assert status == 201, (status, sub)

    assert sub["text_answer"] == 'готово'

    status, review = api.post(
        f"/assignments/{assignment['id']}/review",
        token=teacher["token"],
        body={"status": "needs_fix", "comment": "fix please"},
    )
    assert status == 200, review

    assert review["status"] == "needs_fix" and review["text_answer"] == "готово", review

    status, sub = api.post(
        f"/assignments/{assignment['id']}/submit",
        token=student["token"],
        body={"text_answer": "готово2", "file_ids": []},
    )

    assert status == 201, (status, sub)

    assert sub["text_answer"] == 'готово2'

    status, review = api.post(
        f"/assignments/{assignment['id']}/review",
        token=teacher["token"],
        body={"status": "accepted", "comment": "good job"},
    )
    assert status == 200, review

    assert review["status"] == "accepted" and review["text_answer"] == "готово2", review

    status, sub = api.post(
        f"/assignments/{assignment['id']}/submit",
        token=student["token"],
        body={"text_answer": "готово3", "file_ids": []},
    )

    assert status == 409, (status, sub)

    status, assgn = api.get(f"/assignments/{assignment['id']}", token=student["token"])

    assert status == 200, (status, assgn)

    assert assgn["status"] == "done" and len(assgn["submissions"]) == 2, assgn

    status, assgn = api.get(f"/assignments/{assignment['id']}", token=teacher["token"])

    assert status == 200, (status, assgn)

    assert assgn["status"] == "done" and len(assgn["submissions"]) == 2, assgn

