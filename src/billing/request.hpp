#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

// Requests to the AskDash API / logged in GD-acc / Argon
namespace billing
{
  using RequestFactory = std::function<geode::utils::web::WebRequest()>;
  using ResponseFuture = arc::Future<geode::Result<geode::utils::web::WebResponse>>;

  std::string apiUrl(std::string_view path);
  ResponseFuture send(std::string method, std::string url, RequestFactory makeRequest);
}
