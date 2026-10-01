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

  // ! --- Login headers for requests outside geode::utils::web (live talk WebSocket) --- !
  using Headers = std::vector<std::pair<std::string, std::string>>;

  arc::Future<geode::Result<Headers>> authHeaders();
  // Forgets the login token, the next authHeaders() logs in again
  void clearAuth();
}
