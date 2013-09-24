//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "beast/modules/beast_core/beast_core.h"

#include "ripple_json.h"

#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>

// For json/
//
#ifdef JSON_USE_CPPTL
# include <cpptl/conststring.h>
#endif
#ifndef JSON_USE_SIMPLE_INTERNAL_ALLOCATOR
#include "impl/json_batchallocator.h"
#endif

#define JSON_ASSERT_UNREACHABLE assert( false )
#define JSON_ASSERT( condition ) assert( condition );  // @todo <= change this into an exception throw
#define JSON_ASSERT_MESSAGE( condition, message ) if (!( condition )) throw std::runtime_error( message );

#include "impl/json_reader.cpp"
#include "impl/json_value.cpp"
#include "impl/json_writer.cpp"
