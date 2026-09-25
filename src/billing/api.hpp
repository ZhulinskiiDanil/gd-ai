#pragma once

#include "request.hpp"

namespace billing
{
  struct ApiError
  {
    std::string message;

    bool upgrade = false;
  };

  template <class T>
  using ApiResult = geode::Result<T, ApiError>;

  ApiResult<matjson::Value> parseJson(
      geode::Result<geode::utils::web::WebResponse> const &response);

  struct Status
  {
    std::string plan;
    std::string planName;
    bool unlimited = false;
    int spentPercent = 0;
  };

  struct PlanInfo
  {
    std::string name;
    std::string price;
  };

  struct Plans
  {
    std::vector<PlanInfo> plans;
    std::string buyUrl;
  };

  // GET /me
  ResponseFuture fetchStatus();
  ApiResult<Status> parseStatus(geode::Result<geode::utils::web::WebResponse> const &response);

  // GET /plans
  ResponseFuture fetchPlans();
  ApiResult<Plans> parsePlans(geode::Result<geode::utils::web::WebResponse> const &response);

  // POST /license { key }
  ResponseFuture activateKey(std::string key);

  // Free: 40%
  std::string describe(Status const &status);
}
