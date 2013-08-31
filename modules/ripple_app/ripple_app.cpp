//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

// This file should only hold the Application object .cpp. It depends on
// everything else. Nothing else should depend on the Application
// implementation (although they will depend on its abstract interface)
//

namespace ripple
{

 #include "boost/ripple_IoService.h" // deprecated
#include "boost/ripple_IoService.cpp" // deprecated

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4244) // conversion, possible loss of data
# pragma warning (disable: 4018) // signed/unsigned mismatch
#endif
#include "main/ripple_Application.cpp"
#ifdef _MSC_VER
# pragma warning (pop)
#endif

}
