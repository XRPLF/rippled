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

#ifndef RIPPLE_SOCIDB_H_INCLUDED
#define RIPPLE_SOCIDB_H_INCLUDED

/** An embedded database wrapper with an intuitive, type-safe interface.

    This collection of classes let's you access embedded SQLite databases
    using C++ syntax that is very similar to regular SQL.

    This module requires the @ref beast_sqlite external module.
*/

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning( \
    disable : 4355)  // 'this' : used in base member initializer list
#endif

#include <core/soci.h>
#include <boost/variant.hpp>

namespace ripple {
class SociConfig {
public:
    virtual soci::backend_factory const& backendFactory () const = 0;
    virtual std::string connectionString () const = 0;
};

class BasicConfig;

std::unique_ptr<SociConfig> make_SociConfig (BasicConfig const& config,
                                             std::string dbName);
}

#if _MSC_VER
#pragma warning(pop)
#endif

#endif
