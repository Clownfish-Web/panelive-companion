#pragma once

#include <studio_remote/transport/request_admission.hpp>

#include <string_view>

namespace studio_remote
{
enum class HttpRequestMethod
{
  get,
  post,
  put,
  delete_
};

// Only the transport boundary knows route spellings. Admission policy receives a typed scope.
RequestScope http_request_scope(HttpRequestMethod method, std::string_view path);
} // namespace studio_remote
