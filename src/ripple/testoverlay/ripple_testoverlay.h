//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TESTOVERLAY_H_INCLUDED
#define RIPPLE_TESTOVERLAY_H_INCLUDED

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/unordered_set.hpp>

#include "beast/modules/beast_core/beast_core.h"

/** Provides a template based peer to peer network simulator.

    A TestOverlay::Network simulates an entire peer to peer network.
    It provides peer connectivity and message passing services, while
    allowing domain specific customization through user provided types.

    This system is designed to allow business logic to be exercised
    in unit tests, using a simulated large scale network.
*/

namespace ripple
{

using namespace beast;

#  include "api/Results.h"
#  include "api/SimplePayload.h"
#  include "api/MessageType.h"
#  include "api/ConnectionType.h"
#  include "api/PeerType.h"
#  include "api/NetworkType.h"
#  include "api/StateBase.h"
#  include "api/PeerLogicBase.h"
#  include "api/InitPolicy.h"
# include "api/ConfigType.h"

}

#endif
