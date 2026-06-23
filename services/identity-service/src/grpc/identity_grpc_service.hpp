#pragma once

#include <tutorflow/identity_service.usrv.pb.hpp>

#include "domain/identity_service.hpp"

namespace tutorflow::identity {

class IdentityGrpcService final
    : public tutorflow::identity::v1::IdentityServiceBase::Component {
public:
    static constexpr std::string_view kName = "identity-grpc-service";

    IdentityGrpcService(const userver::components::ComponentConfig& config,
                        const userver::components::ComponentContext& context);

    RegisterResult Register(CallContext& context,
                            tutorflow::identity::v1::RegisterRequest&& request) override;
    LoginResult Login(CallContext& context,
                      tutorflow::identity::v1::LoginRequest&& request) override;
    ValidateTokenResult ValidateToken(
        CallContext& context,
        tutorflow::identity::v1::ValidateTokenRequest&& request) override;
    ChangePasswordResult ChangePassword(
        CallContext& context,
        tutorflow::identity::v1::ChangePasswordRequest&& request) override;
    GetUserResult GetUser(CallContext& context,
                          tutorflow::identity::v1::GetUserRequest&& request) override;
    GetStudentResult GetStudent(
        CallContext& context,
        tutorflow::identity::v1::GetStudentRequest&& request) override;
    ListStudentsResult ListStudents(
        CallContext& context,
        tutorflow::identity::v1::ListStudentsRequest&& request) override;
    CreateStudentResult CreateStudent(
        CallContext& context,
        tutorflow::identity::v1::CreateStudentRequest&& request) override;
    CheckTeacherStudentAccessResult CheckTeacherStudentAccess(
        CallContext& context,
        tutorflow::identity::v1::CheckTeacherStudentAccessRequest&& request) override;

private:
    IdentityService& service_;
};

}  // namespace tutorflow::identity
