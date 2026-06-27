#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include "domain/models.hpp"

namespace tutorflow::common {
struct AuthContext;
}

namespace tutorflow::clients {
class GrpcIdentityClient;
}

namespace tutorflow::chat {

class ChatRepository;

class ChatService final : public userver::components::LoggableComponentBase {
 public:
  static constexpr std::string_view kName = "chat-domain-service";

  ChatService(const userver::components::ComponentConfig& config,
              const userver::components::ComponentContext& context);

  Dialog CreateDialog(const tutorflow::common::AuthContext& auth,
                      const std::string& other_user_id) const;
  std::vector<Dialog> ListDialogs(
      const tutorflow::common::AuthContext& auth) const;
  Message SendMessage(const tutorflow::common::AuthContext& auth,
                      const std::string& dialog_id, const std::string& text,
                      const std::vector<std::string>& file_ids) const;
  std::vector<Message> ListMessages(const tutorflow::common::AuthContext& auth,
                                    const std::string& dialog_id,
                                    const std::optional<std::string>& before,
                                    int limit) const;
  ReadMarker MarkRead(const tutorflow::common::AuthContext& auth,
                      const std::string& dialog_id,
                      const std::string& up_to_message_id) const;

 private:
  ChatRepository& repository_;
  tutorflow::clients::GrpcIdentityClient& identity_;
};

}  // namespace tutorflow::chat
