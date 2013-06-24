//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_core module.

    @file ripple_core.cpp
    @ingroup ripple_core
*/

#include "ripple_core.h"

#include <fstream>

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>

#include "functional/ripple_Config.cpp"
#include "functional/ripple_LoadFeeTrack.cpp"
#include "functional/ripple_Job.cpp"
#include "functional/ripple_JobQueue.cpp"
#include "functional/ripple_LoadEvent.cpp"
#include "functional/ripple_LoadMonitor.cpp"
