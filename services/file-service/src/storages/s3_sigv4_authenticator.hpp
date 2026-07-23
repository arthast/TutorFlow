#pragma once

#include <ctime>
#include <functional>
#include <string>
#include <unordered_map>

#include <userver/s3api/authenticators/interface.hpp>
#include <userver/s3api/models/secret.hpp>

namespace tutorflow::file {

class SigV4Authenticator final
    : public userver::s3api::authenticators::Authenticator {
  public:
    using Now = std::function<std::time_t()>;

    SigV4Authenticator(
        std::string access_key, userver::s3api::Secret secret_key,
        std::string region, std::string endpoint_host,
        Now now = [] { return std::time(nullptr); });

    std::unordered_map<std::string, std::string>
    Auth(const userver::s3api::Request& request) const override;

    std::unordered_map<std::string, std::string>
    Sign(const userver::s3api::Request& request,
         std::time_t expires) const override;

  private:
    std::string access_key_;
    userver::s3api::Secret secret_key_;
    std::string region_;
    std::string endpoint_host_;
    Now now_;
};

} // namespace tutorflow::file
