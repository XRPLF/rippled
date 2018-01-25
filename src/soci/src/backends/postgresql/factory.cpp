//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci/postgresql/soci-postgresql.h"
#include "soci/connection-parameters.h"
#include "soci/backend-loader.h"
#include <libpq/libpq-fs.h> // libpq

#ifdef SOCI_POSTGRESQL_NOPARAMS
#ifndef SOCI_POSTGRESQL_NOBINDBYNAME
#define SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOBINDBYNAME
#endif // SOCI_POSTGRESQL_NOPARAMS

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;

namespace // unnamed
{

// iterates the string pointed by i, searching for pairs of key value.
// it returns the position after the value
std::string::const_iterator get_key_value(std::string::const_iterator & i,
                                          std::string::const_iterator const & end,
                                          std::string & key,
                                          std::string & value)
{
    bool in_value = false;
    bool quoted = false;

    key.clear();
    value.clear();

    while (i != end)
    {
        if (in_value == false)
        {
            if (*i == '=')
            {
                in_value = true;
                if (i != end && *(i + 1) == '"')
                {
                    quoted = true;
                    ++i; // jump over the quote
                }
            }
            else if (!isspace(*i))
            {
                key += *i;
            }
        }
        else
        {
            if ((quoted == true && *i == '"') || (quoted == false && isspace(*i)))
            {
                return ++i;
            }
            else
            {
                value += *i;
            }
        }
        ++i;
    }
    return i;
}

// retrieves specific parameters from the
// uniform connect string
std::string chop_connect_string(std::string const & connectString,
    bool & single_row_mode)
{
    std::string pruned_conn_string;
    
    single_row_mode = false;

    std::string key, value;
    std::string::const_iterator i = connectString.begin();
    while (i != connectString.end())
    {
        i = get_key_value(i, connectString.end(), key, value);
        if (key == "singlerow" || key == "singlerows")
        {
            single_row_mode = (value == "true" || value == "yes");
        }
        else
        {
            if (pruned_conn_string.empty() == false)
            {
                pruned_conn_string += ' ';
            }

            pruned_conn_string += key + '=' + value;
        }
    }

    return pruned_conn_string;
}

} // unnamed namespace

// concrete factory for Empty concrete strategies
postgresql_session_backend * postgresql_backend_factory::make_session(
     connection_parameters const & parameters) const
{
    bool single_row_mode;

    const std::string pruned_conn_string =
        chop_connect_string(parameters.get_connect_string(), single_row_mode);

    connection_parameters pruned_parameters(parameters);
    pruned_parameters.set_connect_string(pruned_conn_string);
    
    return new postgresql_session_backend(pruned_parameters, single_row_mode);
}

postgresql_backend_factory const soci::postgresql;

extern "C"
{

// for dynamic backend loading
SOCI_POSTGRESQL_DECL backend_factory const * factory_postgresql()
{
    return &soci::postgresql;
}

SOCI_POSTGRESQL_DECL void register_factory_postgresql()
{
    soci::dynamic_backends::register_backend("postgresql", soci::postgresql);
}

} // extern "C"
