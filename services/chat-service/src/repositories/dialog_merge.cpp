#include "repositories/dialog_merge.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tutorflow::chat {

std::vector<Dialog> MergeDialogs(std::vector<ShardDialog> dialogs) {
  std::sort(dialogs.begin(), dialogs.end(), [](const ShardDialog& lhs,
                                                const ShardDialog& rhs) {
    if (lhs.dialog.last_message_at.has_value() !=
        rhs.dialog.last_message_at.has_value()) {
      return lhs.dialog.last_message_at.has_value();
    }
    if (lhs.last_message_at_order != rhs.last_message_at_order) {
      return lhs.last_message_at_order > rhs.last_message_at_order;
    }
    if (lhs.created_at_order != rhs.created_at_order) {
      return lhs.created_at_order > rhs.created_at_order;
    }
    return lhs.dialog.id < rhs.dialog.id;
  });

  std::unordered_set<std::string> seen_ids;
  std::vector<Dialog> merged;
  merged.reserve(dialogs.size());
  for (auto& shard_dialog : dialogs) {
    if (seen_ids.insert(shard_dialog.dialog.id).second) {
      merged.push_back(std::move(shard_dialog.dialog));
    }
  }
  return merged;
}

}  // namespace tutorflow::chat
