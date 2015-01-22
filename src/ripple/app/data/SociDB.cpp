//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

/** An embedded database wrapper with an intuitive, type-safe interface.

    This collection of classes let's you access embedded SQLite databases
    using C++ syntax that is very similar to regular SQL.

    This module requires the @ref beast_sqlite external module.
*/

#include <BeastConfig.h>

#include <ripple/core/ConfigSections.h>
#include <ripple/app/data/SociDB.h>
#include <ripple/core/Config.h>
#include <beast/cxx14/memory.h>  // <memory>
#include <backends/sqlite3/soci-sqlite3.h>
#if ENABLE_SOCI_POSTGRESQL
#include <backends/postgresql/soci-postgresql.h>
#endif
#include <boost/filesystem.hpp>

namespace ripple {
namespace detail {

std::pair<std::string, soci::backend_factory const&>
getSociSqliteInit (std::string const& name,
                     std::string const& dir,
                     std::string const& ext)
{
    if (dir.empty () || name.empty ())
    {
        throw std::runtime_error (
            "Sqlite databases must specify a dir and a name. Name: " +
            name + " Dir: " + dir);
    }
    boost::filesystem::path file (dir);
    if (is_directory (file))
        file /= name + ext;
    return std::make_pair (file.string (), std::ref(soci::sqlite3));
}

#if ENABLE_SOCI_POSTGRESQL
std::pair<std::string, soci::backend_factory const&>
getSociPostgresqlInit (Section const& configSection,
                       std::string const& name)
{
    if (name.empty ())
    {
        throw std::runtime_error (
            "Missing required value for postgresql backend: database name");
    }

    std::string const host(get <std::string> (configSection, "host", ""));
    if (!host.empty())
    {
        throw std::runtime_error (
            "Missing required value in config for postgresql backend: host");
    }

    std::string const user(get <std::string> (configSection, "user", ""));
    if (user.empty ())
    {
        throw std::runtime_error (
            "Missing required value in config for postgresql backend: user");
    }

    int const port = [&configSection]
    {
        std::string const portAsString (
            get <std::string> (configSection, "port", ""));
        if (portAsString.empty ())
        {
            throw std::runtime_error (
                "Missing required value in config for postgresql backend: "
                "user");
        }
        try
        {
            return std::stoi (portAsString);
        }
        catch (...)
        {
            throw std::runtime_error (
                "The port value in the config for the postgresql backend must "
                "be an integer. Got: " +
                portAsString);
        }
    }();

    std::stringstream s;
    s << "host=" << host << " port=" << port << " dbname=" << name
      << " user=" << user;
    return std::make_pair (s.str (), std::ref(soci::postgresql));
}
#endif  // ENABLE_SOCI_POSTGRESQL

std::pair<std::string, soci::backend_factory const&>
getSociInit (BasicConfig const& config,
             std::string const& dbName)
{
    static const std::string sectionName ("sqdb");
    static const std::string keyName ("backend");
    auto const& section = config.section (sectionName);
    std::string const backendName(get(section, keyName, std::string("sqlite")));

    if (backendName == "sqlite")
    {
        std::string const path = config.legacy ("database_path");
        std::string const ext =
            (dbName == "validators" || dbName == "peerfinder") ? ".sqlite"
                                                               : ".db";
        return detail::getSociSqliteInit(dbName, path, ext);
    }
#if ENABLE_SOCI_POSTGRESQL
    else if (backendName == "postgresql")
    {
        return detail::getSociPostgresqlInit(section, dbName);
    }
#endif
    else
    {
        throw std::runtime_error ("Unsupported soci backend: " + backendName);
    }
}
} // detail

SociConfig::SociConfig(std::pair<std::string, soci::backend_factory const&> init)
        :connectionString_(std::move(init.first)),
         backendFactory_(init.second){}

SociConfig::SociConfig(BasicConfig const& config,
                       std::string const& dbName)
        : SociConfig(detail::getSociInit(config, dbName))
{
}

std::string SociConfig::connectionString () const
{
    return connectionString_;
}

void SociConfig::open(soci::session& s) const
{
    s.open (backendFactory_, connectionString ());
}

void open(soci::session& s,
          BasicConfig const& config,
          std::string const& dbName)
{
    SociConfig c(config, dbName);
    c.open(s);
}
}

