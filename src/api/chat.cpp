#include "chat.hpp"
#include "context.hpp"

#include <chrono>

using namespace geode::prelude;

static constexpr size_t MAX_HISTORY = 20;

billing::ResponseFuture api::chat::sendMessages(
    std::vector<ChatMessage> const &messages)
{
  auto begin = messages.size() > MAX_HISTORY
                   ? messages.end() - MAX_HISTORY
                   : messages.begin();

  auto jsonMessages = matjson::Value::array();

  for (auto it = begin; it != messages.end(); ++it)
  {
    jsonMessages.push(matjson::makeObject({
        {"role", it->role},
        {"content", it->content},
    }));
  }

  auto body = matjson::makeObject({{"messages", jsonMessages}});
  if (Mod::get()->getSettingValue<bool>("send-game-context"))
    body["context"] = api::context::collect();

  // Built again if the login has to be refreshed
  return billing::send("POST", billing::apiUrl("/chat"), [body = std::move(body)]
                       {
    web::WebRequest request;
    request.header("Content-Type", "application/json");
    request.bodyJSON(body);
    request.timeout(std::chrono::seconds(60));
    return request; });
}

billing::ApiResult<std::string> api::chat::parseReply(
    Result<web::WebResponse> const &response)
{
  GEODE_UNWRAP_INTO(auto json, billing::parseJson(response));

  auto reply = json["reply"].asString();

  if (reply.isErr())
    return Err(billing::ApiError{"Invalid server response"});

  return Ok(reply.unwrap());
}
