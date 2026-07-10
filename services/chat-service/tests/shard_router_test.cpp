#include "repositories/shard_router.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace tutorflow::chat {
namespace {

TEST(ChatShardRouter, BuildsKnownUuidV5DialogId) {
  EXPECT_EQ(MakeDialogId("11111111-1111-4111-8111-111111111111",
                         "22222222-2222-4222-8222-222222222222"),
            "95b761ed-5fa4-59be-8272-a8c5905810cd");
}

TEST(ChatShardRouter, NormalizesParticipantIdsToLowercase) {
  EXPECT_EQ(MakeDialogId("AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA",
                         "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"),
            "08f4b673-3532-55a5-b27f-fbe4734e4960");
  EXPECT_EQ(MakeDialogId("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                         "BBBBBBBB-BBBB-4BBB-8BBB-BBBBBBBBBBBB"),
            "08f4b673-3532-55a5-b27f-fbe4734e4960");
}

TEST(ChatShardRouter, SetsUuidV5VersionAndRfc4122Variant) {
  const auto id = MakeDialogId("11111111-1111-4111-8111-111111111111",
                               "22222222-2222-4222-8222-222222222222");

  ASSERT_EQ(id.size(), 36);
  EXPECT_EQ(id[14], '5');
  EXPECT_NE(std::string{"89ab"}.find(id[19]), std::string::npos);
}

TEST(ChatShardRouter, UsesNormalizedUuidForFnvShardIndex) {
  EXPECT_EQ(ShardIndexForDialogId("95b761ed-5fa4-59be-8272-a8c5905810cd", 2),
            1);
  EXPECT_EQ(ShardIndexForDialogId("08F4B673-3532-55A5-B27F-FBE4734E4960", 2),
            0);
}

TEST(ChatShardRouter, RejectsZeroShards) {
  EXPECT_THROW(
      ShardIndexForDialogId("95b761ed-5fa4-59be-8272-a8c5905810cd", 0),
      std::invalid_argument);
}

TEST(ChatShardRouter, RejectsEmptyClusterList) {
  EXPECT_THROW(ShardRouter(std::vector<ShardRouter::ClusterPtr>{}),
               std::invalid_argument);
}

}  // namespace
}  // namespace tutorflow::chat
