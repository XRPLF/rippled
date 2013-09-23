//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_basics.h"

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/asio.hpp> // For StringUtilities.cpp

#include <fstream> // for Log files

//------------------------------------------------------------------------------

// For Sustain Linux variants
//
// VFALCO TODO Rewrite Sustain to use beast::Process
#ifdef __linux__
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif
#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/wait.h>
#endif

//------------------------------------------------------------------------------

namespace ripple
{

#include "containers/RangeSet.cpp"
#include "containers/TaggedCache.cpp"

#include "log/Log.cpp"
#include "log/LogFile.cpp"
#include "log/LogJournal.cpp"
#include "log/LogPartition.cpp"
#include "log/LogSink.cpp"

#include "utility/CountedObject.cpp"
#include "utility/IniFile.cpp"
#include "utility/StringUtilities.cpp"
#include "utility/Sustain.cpp"
#include "utility/ThreadName.cpp"
#include "utility/Time.cpp"
#include "utility/UptimeTimer.cpp"

}
