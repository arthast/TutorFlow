#include "repositories/dialog_merge.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace tutorflow::chat {
namespace {

ShardDialog MakeDialog(std::string id, std::string created_at,
                       std::optional<std::string> last_message_at,
                       std::int64_t created_at_order,
                       std::int64_t last_message_at_order = 0) {
  return ShardDialog{
      .dialog = Dialog{
          .id = std::move(id),
          .teacher_id = "teacher",
          .student_id = "student",
          .created_at = std::move(created_at),
          .last_message_at = std::move(last_message_at),
          .unread_count = 0,
          .last_message = std::nullopt,
      },
      .created_at_order = created_at_order,
      .last_message_at_order = last_message_at_order,
  };
}

TEST(ChatDialogMerge, OrdersLastMessageDescendingWithNullsLast) {
  std::vector<ShardDialog> dialogs;
  dialogs.push_back(MakeDialog("no-message-newer", "2026-07-10T12:00:00Z",
                               std::nullopt, 120));
  dialogs.push_back(MakeDialog("older-message", "2026-07-10T08:00:00Z",
                               "2026-07-10T10:00:00Z", 80, 100));
  dialogs.push_back(MakeDialog("newer-message", "2026-07-10T07:00:00Z",
                               "2026-07-10T11:00:00Z", 70, 110));
  dialogs.push_back(MakeDialog("no-message-older", "2026-07-10T09:00:00Z",
                               std::nullopt, 90));

  const auto merged = MergeDialogs(std::move(dialogs));

  ASSERT_EQ(merged.size(), 4);
  EXPECT_EQ(merged[0].id, "newer-message");
  EXPECT_EQ(merged[1].id, "older-message");
  EXPECT_EQ(merged[2].id, "no-message-newer");
  EXPECT_EQ(merged[3].id, "no-message-older");
}

TEST(ChatDialogMerge, UsesCreatedAtTieBreaker) {
  std::vector<ShardDialog> dialogs;
  dialogs.push_back(MakeDialog("older", "2026-07-10T08:00:00Z",
                               "2026-07-10T11:00:00Z", 80, 110));
  dialogs.push_back(MakeDialog("newer", "2026-07-10T09:00:00Z",
                               "2026-07-10T11:00:00Z", 90, 110));

  const auto merged = MergeDialogs(std::move(dialogs));

  ASSERT_EQ(merged.size(), 2);
  EXPECT_EQ(merged[0].id, "newer");
  EXPECT_EQ(merged[1].id, "older");
}

TEST(ChatDialogMerge, RemovesDuplicateDialogIds) {
  std::vector<ShardDialog> dialogs;
  dialogs.push_back(MakeDialog("same-id", "2026-07-10T08:00:00Z",
                               "2026-07-10T11:00:00Z", 80, 110));
  dialogs.push_back(dialogs.front());

  const auto merged = MergeDialogs(std::move(dialogs));

  ASSERT_EQ(merged.size(), 1);
  EXPECT_EQ(merged[0].id, "same-id");
}

TEST(ChatDialogMerge, UsesSubsecondOrderKeyAcrossShards) {
  std::vector<ShardDialog> dialogs;
  dialogs.push_back(MakeDialog("later", "2026-07-10T09:47:41Z",
                               "2026-07-10T09:47:41Z", 100, 995406));
  dialogs.push_back(MakeDialog("earlier", "2026-07-10T09:47:41Z",
                               "2026-07-10T09:47:41Z", 200, 960771));

  const auto merged = MergeDialogs(std::move(dialogs));

  ASSERT_EQ(merged.size(), 2);
  EXPECT_EQ(merged[0].id, "later");
  EXPECT_EQ(merged[1].id, "earlier");
}

}  // namespace
}  // namespace tutorflow::chat
