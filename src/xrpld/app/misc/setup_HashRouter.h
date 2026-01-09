#ifndef XRPLD_APP_MISC_SETUP_HASHROUTER_H_INCLUDED
#define XRPLD_APP_MISC_SETUP_HASHROUTER_H_INCLUDED

#include <xrpld/app/misc/HashRouter.h>

namespace xrpl {

// Forward declaration
class Config;

/** Create HashRouter setup from configuration */
HashRouter::Setup
setup_HashRouter(Config const& config);

}  // namespace xrpl

#endif
