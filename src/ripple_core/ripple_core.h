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

# include "functional/ripple_ConfigSections.h"
#include "functional/ripple_Config.h"
#include "functional/ripple_ILoadFeeTrack.h"
#  include "functional/ripple_LoadEvent.h"
#  include "functional/ripple_LoadMonitor.h"
# include "functional/ripple_Job.h"
#include "functional/ripple_JobQueue.h"
# include "functional/LoadType.h"
#include "functional/LoadSource.h"

#include "node/NodeObject.h"
#include "node/NodeStore.h"

#include "peerfinder/ripple_PeerFinder.h"
#include "validator/Validators.h"

}

#endif
