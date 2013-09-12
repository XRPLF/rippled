//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_validators.h"

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
//#include <boost/algorithm/string.hpp>
//#include <boost/foreach.hpp>
//#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "../testoverlay/ripple_testoverlay.h" // for unit test

namespace ripple
{

# include "impl/ValidatorsUtilities.h"
#include "impl/ValidatorsUtilities.cpp"
#  include "impl/ValidatorSourceFile.h"
#  include "impl/ValidatorSourceStrings.h"
#  include "impl/ValidatorSourceURL.h"
#include "impl/ValidatorSourceFile.cpp"
#include "impl/ValidatorSourceStrings.cpp"
#include "impl/ValidatorSourceURL.cpp"
#include "impl/Validators.cpp"

}
