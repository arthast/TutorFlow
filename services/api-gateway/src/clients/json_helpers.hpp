#pragma once

// Small JSON-edge helpers shared by the gateway gRPC clients: nullable strings,
// repeated-string arrays, and request body string-array parsing with the same
// validation the domain REST handlers used.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::gateway {

inline userver::formats::json::Value NullableString(bool has_value,
                                                    const std::string& value) {
  namespace json = userver::formats::json;
  return has_value ? json::ValueBuilder(value).ExtractValue()
                   : json::ValueBuilder(nullptr).ExtractValue();
}

// Builds a JSON array from any iterable of strings (e.g. a proto repeated
// field), so callers need not pull in protobuf headers here.
template <typename Items>
userver::formats::json::Value StringArray(const Items& items) {
  userver::formats::json::ValueBuilder array(
      userver::formats::common::Type::kArray);
  for (const auto& item : items) {
    array.PushBack(item);
  }
  return array.ExtractValue();
}

inline std::vector<std::string> RequireStringArray(
    const userver::formats::json::Value& body, std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    return {};
  }
  if (!body[key].IsArray()) {
    throw tutorflow::common::ServiceError::Validation(
        "field must be an array: " + key);
  }
  std::vector<std::string> values;
  for (const auto& item : body[key]) {
    auto value = item.As<std::string>("");
    if (value.empty()) {
      throw tutorflow::common::ServiceError::Validation(
          "array item must not be empty: " + key);
    }
    values.push_back(std::move(value));
  }
  return values;
}

}  // namespace tutorflow::gateway
