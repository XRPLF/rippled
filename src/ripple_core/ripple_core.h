//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_H_INCLUDED
#define RIPPLE_CORE_H_INCLUDED

// VFALCO TODO For UniformResourceLocator, remove asap
#include "beast/modules/beast_asio/beast_asio.h"

#include "../ripple/frame/ripple_frame.h"
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
