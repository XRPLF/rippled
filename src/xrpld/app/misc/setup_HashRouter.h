#pragma once

#include <xrpl/core/HashRouter.h>

namespace xrpl {

// Forward declaration
class Config;

/** Create HashRouter setup from configuration */
HashRouter::Setup
setupHashRouter(Config const& config);

}  // namespace xrpl
