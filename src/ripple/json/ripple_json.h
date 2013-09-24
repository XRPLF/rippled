//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_JSON_H_INCLUDED
#define RIPPLE_JSON_H_INCLUDED

#include "beast/beast/Config.h"

#include <deque>
#include <stack>
#include <vector>

// For json/
//
// VFALCO TODO Clean up these one-offs
#include "api/json_config.h" // Needed before these cpptl includes
#ifndef JSON_USE_CPPTL_SMALLMAP
# include <map>
#else
# include <cpptl/smallmap.h>
#endif
#ifdef JSON_USE_CPPTL
# include <cpptl/forwards.h>
#endif

#include "api/json_forwards.h"
#include "api/json_features.h"
#include "api/json_value.h"
#include "api/json_reader.h"
#include "api/json_writer.h"

#endif
