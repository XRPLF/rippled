//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_H_INCLUDED
#define RIPPLE_VALIDATORS_H_INCLUDED

// VFALCO TODO Remove buffers/beast_asio dependency
//
// VFALCO NOTE It is unfortunate that we are exposing boost/asio.hpp
//             needlessly. Its only required because of the buffers types.
//             The HTTPClient interface doesn't need asio (although the
//             implementation does. This is also required for
//             UniformResourceLocator.
//
#include "beast/modules/beast_asio/beast_asio.h"

#include "../ripple_basics/ripple_basics.h"
#include "../ripple_data/ripple_data.h"

namespace ripple
{

using namespace beast;

# include "api/Types.h"
# include "api/Source.h"
#include "api/Manager.h"

}

#endif
