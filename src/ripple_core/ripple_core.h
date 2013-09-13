//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_RIPPLEHEADER
#define RIPPLE_CORE_RIPPLEHEADER

// For Validators
//
// VFALCO NOTE It is unfortunate that we are exposing boost/asio.hpp
//             needlessly. Its only required because of the buffers types.
//             The HTTPClient interface doesn't need asio (although the
//             implementation does. This is also reuqired for
//             UniformResourceLocator.
//
#include "beast/modules/beast_asio/beast_asio.h"

#include "../ripple_basics/ripple_basics.h"
#include "../ripple_data/ripple_data.h"

namespace ripple
{

// Order matters

# include "functional/ConfigSections.h"
#include "functional/Config.h"
#include "functional/ILoadFeeTrack.h"
#  include "functional/LoadEvent.h"
#  include "functional/LoadMonitor.h"
# include "functional/Job.h"
#include "functional/JobQueue.h"
# include "functional/LoadType.h"
#include "functional/LoadSource.h"

#include "node/NodeObject.h"
#include "node/NodeStore.h"

#include "peerfinder/PeerFinder.h"

}

#endif
