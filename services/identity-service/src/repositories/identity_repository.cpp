#include "repositories/identity_repository.hpp"

#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::identity {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave  = pg::ClusterHostType::kSlave;


User RowToUser(const pg::Row& row) {
    return User{
        row["id"].As<std::string>(),
        row["email"].As<std::string>(),
        row["role"].As<std::string>(),
        row["display_name"].As<std::string>(),
        row["created_at"].As<std::string>(),
    };
}

constexpr std::string_view kUserWithProfileSelect = R"(
SELECT
    u.id::text,
    u.email,
    u.role,
    COALESCE(tp.display_name, sp.display_name, '') AS display_name,
    to_char(u.created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
FROM users u
LEFT JOIN teacher_profiles tp ON tp.user_id = u.id
LEFT JOIN student_profiles sp ON sp.user_id = u.id
)";

StudentLink RowToStudentLink(const pg::Row& row) {
    StudentLink s;
    s.id          = row["id"].As<std::string>();
    s.teacher_id  = row["teacher_id"].As<std::string>();
    s.student_id  = row["student_id"].As<std::string>();
    s.display_name = row["display_name"].As<std::string>();
    {
        auto v = row["subject"].As<std::optional<std::string>>();
        if (v && !v->empty()) s.subject = v;
    }
    {
        auto v = row["goal"].As<std::optional<std::string>>();
        if (v && !v->empty()) s.goal = v;
    }
    {
        auto v = row["hourly_rate"].As<std::optional<double>>();
        if (v) s.hourly_rate = v;
    }
    s.status     = row["status"].As<std::string>();
    s.created_at = row["created_at"].As<std::string>();
    return s;
}

constexpr std::string_view kStudentLinkSelect = R"(
SELECT
    tsl.id::text,
    tsl.teacher_id::text,
    tsl.student_id::text,
    sp.display_name,
    tsl.subject,
    tsl.goal,
    tsl.hourly_rate::double precision AS hourly_rate,
    tsl.status,
    to_char(tsl.created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
FROM teacher_student_links tsl
JOIN student_profiles sp ON sp.user_id = tsl.student_id
)";

}  // namespace

IdentityRepository::IdentityRepository(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("identity-db")
              .GetCluster()) {}

User IdentityRepository::CreateTeacher(
    const std::string& email, const std::string& password_hash,
    const std::string& display_name,
    const std::optional<std::string>& timezone) const {
    const auto result = pg_->Execute(
        kMaster,
        R"(
WITH new_user AS (
    INSERT INTO users (email, password_hash, role)
    VALUES ($1, $2, 'teacher')
    RETURNING id, email, role, created_at
), _profile AS (
    INSERT INTO teacher_profiles (user_id, display_name, timezone)
    SELECT id, $3, NULLIF($4, '')
    FROM new_user
)
SELECT
    id::text,
    email,
    role,
    $3 AS display_name,
    to_char(created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
FROM new_user
)",
        email, password_hash, display_name, timezone.value_or(""));
    if (result.IsEmpty()) {
        throw tutorflow::common::ServiceError::Internal("failed to create teacher");
    }
    return RowToUser(result[0]);
}

User IdentityRepository::CreateStudent(const std::string& email,
                                       const std::string& password_hash,
                                       const std::string& display_name) const {
    const auto result = pg_->Execute(
        kMaster,
        R"(
WITH new_user AS (
    INSERT INTO users (email, password_hash, role)
    VALUES ($1, $2, 'student')
    RETURNING id, email, role, created_at
), _profile AS (
    INSERT INTO student_profiles (user_id, display_name)
    SELECT id, $3 FROM new_user
)
SELECT
    id::text,
    email,
    role,
    $3 AS display_name,
    to_char(created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
FROM new_user
)",
        email, password_hash, display_name);
    if (result.IsEmpty()) {
        throw tutorflow::common::ServiceError::Internal("failed to create student");
    }
    return RowToUser(result[0]);
}

std::optional<std::pair<User, std::string>>
IdentityRepository::FindUserWithHash(const std::string& email) const {
    const auto result = pg_->Execute(
        kSlave,
        R"(
SELECT
    u.id::text,
    u.email,
    u.role,
    COALESCE(tp.display_name, sp.display_name, '') AS display_name,
    to_char(u.created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
    u.password_hash
FROM users u
LEFT JOIN teacher_profiles tp ON tp.user_id = u.id
LEFT JOIN student_profiles sp ON sp.user_id = u.id
WHERE u.email = $1
)",
        email);
    if (result.IsEmpty()) return std::nullopt;
    auto user = RowToUser(result[0]);
    std::string hash = result[0]["password_hash"].As<std::string>();
    return std::make_pair(std::move(user), std::move(hash));
}

std::optional<std::pair<User, std::string>>
IdentityRepository::FindUserWithHashById(const std::string& id) const {
    const auto result = pg_->Execute(
        kSlave,
        R"(
SELECT
    u.id::text,
    u.email,
    u.role,
    COALESCE(tp.display_name, sp.display_name, '') AS display_name,
    to_char(u.created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
    u.password_hash
FROM users u
LEFT JOIN teacher_profiles tp ON tp.user_id = u.id
LEFT JOIN student_profiles sp ON sp.user_id = u.id
WHERE u.id = $1::uuid
)",
        id);
    if (result.IsEmpty()) return std::nullopt;
    auto user = RowToUser(result[0]);
    std::string hash = result[0]["password_hash"].As<std::string>();
    return std::make_pair(std::move(user), std::move(hash));
}

void IdentityRepository::UpdatePasswordHash(
    const std::string& user_id, const std::string& password_hash) const {
    const auto result = pg_->Execute(
        kMaster,
        "UPDATE users SET password_hash = $2 WHERE id = $1::uuid",
        user_id, password_hash);
    if (result.RowsAffected() == 0) {
        throw tutorflow::common::ServiceError::NotFound("user not found");
    }
}

std::optional<User> IdentityRepository::FindUserById(
    const std::string& id) const {
    const auto result = pg_->Execute(
        kSlave,
        std::string{kUserWithProfileSelect} + " WHERE u.id = $1::uuid",
        id);
    if (result.IsEmpty()) return std::nullopt;
    return RowToUser(result[0]);
}

CheckAccessResult IdentityRepository::CheckAccess(
    const std::string& teacher_id, const std::string& student_id) const {
    const auto result = pg_->Execute(
        kSlave,
        R"(
SELECT
    (tsl.id IS NOT NULL) AS allowed,
    COALESCE(tsl.status, '')  AS status,
    tsl.hourly_rate::double precision AS hourly_rate
FROM (SELECT $1::uuid AS tid, $2::uuid AS sid) params
LEFT JOIN teacher_student_links tsl
       ON tsl.teacher_id = params.tid AND tsl.student_id = params.sid
)",
        teacher_id, student_id);
    if (result.IsEmpty()) {
        return CheckAccessResult{false, {}, std::nullopt};
    }
    const auto& row = result[0];
    CheckAccessResult r;
    r.allowed     = row["allowed"].As<bool>();
    r.status      = row["status"].As<std::string>();
    r.hourly_rate = row["hourly_rate"].As<std::optional<double>>();
    return r;
}

StudentLink IdentityRepository::CreateStudentWithLink(
    const std::string& teacher_id, const CreateStudentRequest& req,
    const std::string& password_hash) const {
    const auto result = pg_->Execute(
        kMaster,
        R"(
WITH new_user AS (
    INSERT INTO users (email, password_hash, role)
    VALUES ($1, $2, 'student')
    RETURNING id
), _profile AS (
    INSERT INTO student_profiles (user_id, display_name)
    SELECT id, $3 FROM new_user
    RETURNING user_id, display_name
), new_link AS (
    INSERT INTO teacher_student_links
        (teacher_id, student_id, subject, goal, hourly_rate, status)
    SELECT $4::uuid, nu.id, NULLIF($5, ''), NULLIF($6, ''), $7, 'active'
    FROM new_user nu
    RETURNING
        id::text,
        teacher_id::text,
        student_id::text,
        subject,
        goal,
        hourly_rate::double precision AS hourly_rate,
        status,
        to_char(created_at AT TIME ZONE 'UTC',
                'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
)
SELECT
    nl.id,
    nl.teacher_id,
    nl.student_id,
    p.display_name,
    nl.subject,
    nl.goal,
    nl.hourly_rate,
    nl.status,
    nl.created_at
FROM new_link nl
JOIN _profile p ON p.user_id = nl.student_id::uuid
)",
        req.email,
        password_hash,
        req.display_name,
        teacher_id,
        req.subject.value_or(""),
        req.goal.value_or(""),
        req.hourly_rate);

    if (result.IsEmpty()) {
        throw tutorflow::common::ServiceError::Internal("failed to create student");
    }
    return RowToStudentLink(result[0]);
}

std::optional<StudentLink> IdentityRepository::FindStudentLink(
    const std::string& student_id) const {
    const auto result = pg_->Execute(
        kSlave,
        std::string{kStudentLinkSelect} +
            " WHERE tsl.student_id = $1::uuid ORDER BY tsl.created_at DESC, tsl.id DESC LIMIT 1",
        student_id);
    if (result.IsEmpty()) return std::nullopt;
    return RowToStudentLink(result[0]);
}

std::vector<StudentLink> IdentityRepository::ListStudentsForTeacher(
    const std::string& teacher_id) const {
    const auto result = pg_->Execute(
        kSlave,
        std::string{kStudentLinkSelect} +
            " WHERE tsl.teacher_id = $1::uuid ORDER BY tsl.created_at DESC, tsl.id DESC",
        teacher_id);
    std::vector<StudentLink> items;
    items.reserve(result.Size());
    for (const auto& row : result) {
        items.push_back(RowToStudentLink(row));
    }
    return items;
}

}  // namespace tutorflow::identity
