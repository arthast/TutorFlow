#pragma once

#include <string_view>

// Машинные коды ошибок — единый перечень для всех сервисов (PLAN §6).
// Держим в libs/common, чтобы коды не расходились между сервисами.
namespace tutorflow::common::error_code {

inline constexpr std::string_view kValidation = "validation_error";          // 400
inline constexpr std::string_view kUnauthorized = "unauthorized";            // 401
inline constexpr std::string_view kForbidden = "forbidden";                  // 403
inline constexpr std::string_view kNotFound = "not_found";                   // 404
inline constexpr std::string_view kConflict = "conflict";                    // 409
inline constexpr std::string_view kPayloadTooLarge = "payload_too_large";    // 413
inline constexpr std::string_view kUnsupportedMediaType =
    "unsupported_media_type";                                                // 415
inline constexpr std::string_view kBusinessRule = "business_rule";           // 422
inline constexpr std::string_view kInternal = "internal_error";              // 500

}  // namespace tutorflow::common::error_code
