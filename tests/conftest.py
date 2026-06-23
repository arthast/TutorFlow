import pytest

from tests import _client as api


@pytest.fixture
def teacher():
    return api.register_teacher()


@pytest.fixture
def student(teacher):
    return api.create_student(teacher["token"])


@pytest.fixture
def lesson(teacher, student):
    return api.create_lesson(teacher["token"], student["user_id"])
