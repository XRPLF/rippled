#pragma once

#include <chrono>

namespace xrpl {

constexpr std::size_t kFullBelowTargetSize = 524288;
constexpr std::chrono::seconds kFullBelowExpiration = std::chrono::minutes{10};

constexpr std::size_t kMaxPoppedTransactions = 10;

}  // namespace xrpl
