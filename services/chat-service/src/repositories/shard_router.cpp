#include "repositories/shard_router.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <userver/crypto/hash.hpp>

namespace tutorflow::chat {
namespace {

constexpr std::array<unsigned char, 16> kChatDialogNamespace{
    0xb6, 0xf0, 0xd8, 0x96, 0x6d, 0x38, 0x4b, 0x6f,
    0x8e, 0xc4, 0x58, 0xa4, 0xf7, 0xa3, 0xc1, 0xd2};

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::string_view kHex = "0123456789abcdef";

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string FormatUuid(const std::array<unsigned char, 16>& bytes) {
  std::string value;
  value.reserve(36);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) value.push_back('-');
    value.push_back(kHex[(bytes[i] >> 4U) & 0x0fU]);
    value.push_back(kHex[bytes[i] & 0x0fU]);
  }
  return value;
}

}  // namespace

std::string MakeDialogId(const std::string& teacher_id,
                         const std::string& student_id) {
  const auto name = Lowercase(teacher_id) + ":" + Lowercase(student_id);
  const std::string_view namespace_bytes{
      reinterpret_cast<const char*>(kChatDialogNamespace.data()),
      kChatDialogNamespace.size()};
  const auto digest = userver::crypto::hash::Sha1(
      {namespace_bytes, name}, userver::crypto::hash::OutputEncoding::kBinary);
  if (digest.size() < 16) {
    throw std::runtime_error("SHA-1 returned a truncated digest");
  }

  std::array<unsigned char, 16> uuid_bytes{};
  for (std::size_t i = 0; i < uuid_bytes.size(); ++i) {
    uuid_bytes[i] = static_cast<unsigned char>(digest[i]);
  }
  uuid_bytes[6] = static_cast<unsigned char>((uuid_bytes[6] & 0x0fU) | 0x50U);
  uuid_bytes[8] = static_cast<unsigned char>((uuid_bytes[8] & 0x3fU) | 0x80U);
  return FormatUuid(uuid_bytes);
}

std::size_t ShardIndexForDialogId(const std::string& dialog_id,
                                  std::size_t shard_count) {
  if (shard_count == 0) {
    throw std::invalid_argument("chat shard count must be positive");
  }

  std::uint64_t hash = kFnvOffsetBasis;
  for (const unsigned char c : dialog_id) {
    if (c == '-') continue;
    hash ^= static_cast<unsigned char>(std::tolower(c));
    hash *= kFnvPrime;
  }
  return static_cast<std::size_t>(hash % shard_count);
}

ShardRouter::ShardRouter(std::vector<ClusterPtr> clusters)
    : clusters_(std::move(clusters)) {
  if (clusters_.empty()) {
    throw std::invalid_argument("chat shard router requires at least one shard");
  }
}

const ShardRouter::ClusterPtr& ShardRouter::ClusterFor(
    const std::string& dialog_id) const {
  return clusters_[ShardIndexForDialogId(dialog_id, clusters_.size())];
}

const std::vector<ShardRouter::ClusterPtr>& ShardRouter::All() const noexcept {
  return clusters_;
}

}  // namespace tutorflow::chat
