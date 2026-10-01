#pragma once

#include <vector>
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "../billing/api.hpp"
#include "../store/ChatStore.hpp"

namespace api::chat
{
    struct StreamChunk
    {
        std::string delta;
        // Pass as `from` to the next poll
        size_t next = 0;
        // Set once the reply is complete, with its action buttons
        std::optional<ChatMessage> reply;
    };

    // POST {api-url}/chat/stream  { messages: [{ role, content }], context }  ->  { id }, as logged GD-acc
    billing::ResponseFuture startStream(
        std::vector<ChatMessage> const &messages);

    billing::ApiResult<std::string> parseStreamId(
        geode::Result<geode::utils::web::WebResponse> const &response);

    // GET {api-url}/chat/stream/{id}?from=N  ->  { delta, next, done, reply, actions, error }
    billing::ResponseFuture pollStream(std::string id, size_t from);

    billing::ApiResult<StreamChunk> parseStreamChunk(
        geode::Result<geode::utils::web::WebResponse> const &response);
}
