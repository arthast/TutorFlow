#pragma once

#include <cstdint>
#include <vector>

#include "domain/models.hpp"

namespace tutorflow::chat {

struct ShardDialog {
  Dialog dialog;
  std::int64_t created_at_order{};
  std::int64_t last_message_at_order{};
};

std::vector<Dialog> MergeDialogs(std::vector<ShardDialog> dialogs);

}  // namespace tutorflow::chat
