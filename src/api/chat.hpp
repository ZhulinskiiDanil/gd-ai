#pragma once

#include <vector>
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "../billing/api.hpp"
#include "../store/ChatStore.hpp"

namespace api::chat
{
    // POST {api-url}/chat  { messages: [{ role, content }] }, as logged GD-acc
    billing::ResponseFuture sendMessages(
        std::vector<ChatMessage> const &messages);

    billing::ApiResult<std::string> parseReply(
        geode::Result<geode::utils::web::WebResponse> const &response);
}
