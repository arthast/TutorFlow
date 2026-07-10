#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

namespace tutorflow::chat {

std::string MakeDialogId(const std::string& teacher_id,
                         const std::string& student_id);

std::size_t ShardIndexForDialogId(const std::string& dialog_id,
                                  std::size_t shard_count);

class ShardRouter final {
 public:
  using ClusterPtr = userver::storages::postgres::ClusterPtr;

  explicit ShardRouter(std::vector<ClusterPtr> clusters);

  const ClusterPtr& ClusterFor(const std::string& dialog_id) const;
  const std::vector<ClusterPtr>& All() const noexcept;

 private:
  std::vector<ClusterPtr> clusters_;
};

}  // namespace tutorflow::chat
