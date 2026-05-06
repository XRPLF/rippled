#pragma once

#include <chrono>

namespace xrpl {

constexpr std::size_t kFULL_BELOW_TARGET_SIZE = 524288;
constexpr std::chrono::seconds kFULL_BELOW_EXPIRATION = std::chrono::minutes{10};

constexpr std::size_t kMAX_POPPED_TRANSACTIONS = 10;

}  // namespace xrpl
