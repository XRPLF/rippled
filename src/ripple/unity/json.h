//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_JSON_H_INCLUDED
#define RIPPLE_JSON_H_INCLUDED

#include <beast/Config.h>

#include <beast/strings/String.h>
#include <beast/utility/PropertyStream.h>

#include <deque>
#include <stack>
#include <vector>

// For json/
//
// VFALCO TODO Clean up these one-offs
#include <ripple/json/api/json_config.h> // Needed before these cpptl includes
#ifndef JSON_USE_CPPTL_SMALLMAP
#include <map>
#else
#include <cpptl/smallmap.h>
#endif
#ifdef JSON_USE_CPPTL
#include <cpptl/forwards.h>
#endif

#include <ripple/json/api/json_forwards.h>
#include <ripple/json/api/json_features.h>
#include <ripple/json/api/json_value.h>
#include <ripple/json/api/json_reader.h>
#include <ripple/json/api/json_writer.h>

#include <ripple/json/api/JsonPropertyStream.h>

#endif
