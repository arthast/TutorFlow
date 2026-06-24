#include "grpc/identity_grpc_service.hpp"

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/grpc_server_utils.hpp>
#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>

#include "domain/models.hpp"

namespace tutorflow::identity {
namespace {
namespace proto = tutorflow::identity::v1;
namespace common_proto = tutorflow::common::v1;

using tutorflow::clients::InvokeServerUnary;
using tutorflow::clients::ResolveServerAuthContext;

proto::TokenResponse ToProto(const TokenResponse& token) {
    proto::TokenResponse response;
    response.set_access_token(token.access_token);
    response.set_token_type(token.token_type);
    response.set_expires_in(token.expires_in);
    response.set_user_id(token.user_id);
    for (const auto& role : token.roles) {
        response.add_roles(role);
    }
    return response;
}

proto::TokenClaims ToProto(const TokenClaims& claims) {
    proto::TokenClaims response;
    response.set_sub(claims.sub);
    response.set_exp(claims.exp);
    for (const auto& role : claims.roles) {
        response.add_roles(role);
    }
    return response;
}

proto::User ToProto(const User& user) {
    proto::User response;
    response.set_id(user.id);
    response.set_email(user.email);
    response.set_role(user.role);
    response.set_display_name(user.display_name);
    response.set_created_at(user.created_at);
    return response;
}

proto::StudentLink ToProto(const StudentLink& link) {
    proto::StudentLink response;
    response.set_id(link.id);
    response.set_teacher_id(link.teacher_id);
    response.set_student_id(link.student_id);
    response.set_display_name(link.display_name);
    if (link.subject) {
        response.set_subject(*link.subject);
    }
    if (link.goal) {
        response.set_goal(*link.goal);
    }
    if (link.hourly_rate) {
        response.set_hourly_rate(*link.hourly_rate);
    }
    response.set_status(link.status);
    response.set_created_at(link.created_at);
    return response;
}

RegisterRequest FromProto(const proto::RegisterRequest& request) {
    return RegisterRequest{
        .email = request.email(),
        .password = request.password(),
        .role = request.role(),
        .display_name = request.display_name(),
        .timezone = request.has_timezone()
                        ? std::optional<std::string>{request.timezone()}
                        : std::nullopt,
    };
}

LoginRequest FromProto(const proto::LoginRequest& request) {
    return LoginRequest{
        .email = request.email(),
        .password = request.password(),
    };
}

ChangePasswordRequest FromProto(const proto::ChangePasswordRequest& request) {
    return ChangePasswordRequest{
        .current_password = request.current_password(),
        .new_password = request.new_password(),
    };
}

CreateStudentRequest FromProto(const proto::CreateStudentRequest& request) {
    return CreateStudentRequest{
        .email = request.email(),
        .password = request.password(),
        .display_name = request.display_name(),
        .subject = request.has_subject()
                       ? std::optional<std::string>{request.subject()}
                       : std::nullopt,
        .goal = request.has_goal()
                    ? std::optional<std::string>{request.goal()}
                    : std::nullopt,
        .hourly_rate = request.has_hourly_rate()
                           ? std::optional<double>{request.hourly_rate()}
                           : std::nullopt,
    };
}

}  // namespace

IdentityGrpcService::IdentityGrpcService(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : proto::IdentityServiceBase::Component(config, context),
      service_(context.FindComponent<IdentityService>()) {}

IdentityGrpcService::RegisterResult IdentityGrpcService::Register(
    CallContext&, proto::RegisterRequest&& request) {
    return InvokeServerUnary<proto::TokenResponse>(
        [&] { return ToProto(service_.Register(FromProto(request))); });
}

IdentityGrpcService::LoginResult IdentityGrpcService::Login(
    CallContext&, proto::LoginRequest&& request) {
    return InvokeServerUnary<proto::TokenResponse>(
        [&] { return ToProto(service_.Login(FromProto(request))); });
}

IdentityGrpcService::ValidateTokenResult IdentityGrpcService::ValidateToken(
    CallContext&, proto::ValidateTokenRequest&& request) {
    return InvokeServerUnary<proto::TokenClaims>(
        [&] { return ToProto(service_.ValidateToken(request.token())); });
}

IdentityGrpcService::ChangePasswordResult IdentityGrpcService::ChangePassword(
    CallContext& context, proto::ChangePasswordRequest&& request) {
    return InvokeServerUnary<common_proto::Empty>([&] {
        const auto auth = ResolveServerAuthContext(context, request.user());
        if (auth.user_id.empty()) {
            throw tutorflow::common::ServiceError::Unauthorized(
                "missing user context");
        }
        service_.ChangePassword(auth.user_id, FromProto(request));
        return common_proto::Empty{};
    });
}

IdentityGrpcService::GetUserResult IdentityGrpcService::GetUser(
    CallContext&, proto::GetUserRequest&& request) {
    return InvokeServerUnary<proto::User>(
        [&] { return ToProto(service_.GetUser(request.user_id())); });
}

IdentityGrpcService::GetStudentResult IdentityGrpcService::GetStudent(
    CallContext&, proto::GetStudentRequest&& request) {
    return InvokeServerUnary<proto::StudentLink>(
        [&] { return ToProto(service_.GetStudentLink(request.student_id())); });
}

IdentityGrpcService::ListStudentsResult IdentityGrpcService::ListStudents(
    CallContext& context, proto::ListStudentsRequest&& request) {
    return InvokeServerUnary<proto::ListStudentsResponse>([&] {
        const auto auth = ResolveServerAuthContext(context, request.user());
        const std::string teacher_id =
            request.teacher_id().empty() ? auth.user_id : request.teacher_id();
        proto::ListStudentsResponse response;
        for (const auto& student : service_.ListStudents(teacher_id)) {
            *response.add_students() = ToProto(student);
        }
        return response;
    });
}

IdentityGrpcService::CreateStudentResult IdentityGrpcService::CreateStudent(
    CallContext& context, proto::CreateStudentRequest&& request) {
    return InvokeServerUnary<proto::StudentLink>([&] {
        const auto auth = ResolveServerAuthContext(context, request.user());
        tutorflow::common::RequireTeacher(auth);
        return ToProto(service_.CreateStudent(auth.user_id, FromProto(request)));
    });
}

IdentityGrpcService::CheckTeacherStudentAccessResult
IdentityGrpcService::CheckTeacherStudentAccess(
    CallContext&, proto::CheckTeacherStudentAccessRequest&& request) {
    return InvokeServerUnary<proto::CheckTeacherStudentAccessResponse>([&] {
        const auto result =
            service_.CheckAccess(request.teacher_id(), request.student_id());
        proto::CheckTeacherStudentAccessResponse response;
        response.set_allowed(result.allowed);
        if (result.allowed) {
            response.set_status(result.status);
            if (result.hourly_rate) {
                response.set_hourly_rate(*result.hourly_rate);
            }
        }
        return response;
    });
}

}  // namespace tutorflow::identity
