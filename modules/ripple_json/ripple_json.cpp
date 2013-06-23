//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_json module.

    @file ripple_json.cpp
    @ingroup ripple_json
*/

#include "ripple_json.h"

#include <stdexcept>
#include <iomanip>

#ifdef JSON_USE_CPPTL
# include <cpptl/conststring.h>
#endif

// VFALCO TODO eliminate this boost dependency
#include <boost/lexical_cast.hpp>

#ifndef JSON_USE_SIMPLE_INTERNAL_ALLOCATOR
#include "json/json_batchallocator.h"
#endif

#define JSON_ASSERT_UNREACHABLE assert( false )
#define JSON_ASSERT( condition ) assert( condition );  // @todo <= change this into an exception throw
#define JSON_ASSERT_MESSAGE( condition, message ) if (!( condition )) throw std::runtime_error( message );

#if RIPPLE_USE_NAMESPACE
namespace ripple
{
#endif

#include "json/json_reader.cpp"
#include "json/json_value.cpp"
#include "json/json_writer.cpp"

#if RIPPLE_USE_NAMESPACE
}
#endif
