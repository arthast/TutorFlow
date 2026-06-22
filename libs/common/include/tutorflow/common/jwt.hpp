#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tutorflow::common::jwt {

struct Claims {
    std::string sub;
    std::vector<std::string> roles;
    int64_t exp{};
    int64_t iat{};
};

std::string Sign(const Claims& claims, std::string_view secret);

std::optional<Claims> Verify(std::string_view token, std::string_view secret);

}  // namespace tutorflow::common::jwt
