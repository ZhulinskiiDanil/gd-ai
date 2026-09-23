#pragma once

#include <vector>
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "../store/ChatStore.hpp"

namespace api::chat
{
    // POST {api-url}/chat  { messages: [{ role, content }] }
    geode::utils::web::WebFuture sendMessages(
        std::vector<ChatMessage> const &messages);

    geode::Result<std::string> parseReply(
        geode::utils::web::WebResponse const &response);
}
