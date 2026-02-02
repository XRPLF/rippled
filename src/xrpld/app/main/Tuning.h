#ifndef XRPL_APP_MAIN_TUNING_H_INCLUDED
#define XRPL_APP_MAIN_TUNING_H_INCLUDED

#include <chrono>

namespace xrpl {

constexpr std::size_t fullBelowTargetSize = 524288;
constexpr std::chrono::seconds fullBelowExpiration = std::chrono::minutes{10};

constexpr std::size_t maxPoppedTransactions = 10;

}  // namespace xrpl

#endif
