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

/** An embedded database wrapper with an intuitive, type-safe interface.

    This collection of classes let's you access embedded SQLite databases
    using C++ syntax that is very similar to regular SQL.

    This module requires the @ref beast_sqlite external module.
*/

#include <BeastConfig.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/app/data/SociDB.h>
#include <backends/sqlite3/soci-sqlite3.h>
#if defined(ENABLE_SOCI_POSTGRESQL) && ENABLE_SOCI_POSTGRESQL
#include <backends/postgresql/soci-postgresql.h>
#endif
#include <ripple/core/Config.h>
#include <boost/filesystem.hpp>
#include <beast/cxx14/memory.h>  // <memory>

namespace ripple {
namespace detail {
class SociSqliteConfig final : public SociConfig
{
private:
    std::string dbName_;
    std::string dbPath_;
    std::string dbFileExt_;

public:
    SociSqliteConfig (std::string dbName,
                      std::string dbPath,
                      std::string dbFileExt)
        : dbName_ (std::move (dbName))
        , dbPath_ (std::move (dbPath))
        , dbFileExt_ (std::move (dbFileExt))
    {
    }
    virtual soci::backend_factory const& backendFactory () const override
    {
        return soci::sqlite3;
    }
    virtual std::string connectionString () const override
    {
        if (dbPath_.empty () || dbName_.empty ())
        {
            throw std::runtime_error (
                "Sqlite databases must specify a path and a name. Name: " +
                dbName_ + " Path: " + dbPath_);
        }
        boost::filesystem::path file (dbPath_);
        if (is_directory (file))
            file /= dbName_ + dbFileExt_;
        return file.string ();
    }
};

#if defined(ENABLE_SOCI_POSTGRESQL) && ENABLE_SOCI_POSTGRESQL
class SociPostgresqlConfig final : public SociConfig
{
private:
    std::string host_;
    int port_;
    std::string user_;
    std::string dbName_;

public:
    SociPostgresqlConfig () = default;
    explicit SociPostgresqlConfig (Section const& configSection,
                                   std::string dbName);
    virtual soci::backend_factory const& backendFactory () const override
    {
        return soci::postgresql;
    }
    virtual std::string connectionString () const override
    {
        if (host_.empty () || dbName_.empty () || user_.empty ())
        {
            throw std::runtime_error (
                "Postgresql databases must specify a host, port, dbName, and "
                "user.");
        }
        std::stringstream s;
        s << "host=" << host_ << " port=" << port_ << " dbname=" << dbName_
          << " user=" << user_;
        return s.str ();
    }
};

SociPostgresqlConfig::SociPostgresqlConfig (Section const& configSection,
                                            std::string dbName)
    : dbName_ (std::move (dbName))
{
    bool found;
    std::tie (host_, found) = configSection.find ("host");
    if (!found)
    {
        throw std::runtime_error (
            "Missing required value in config for postgresql backend: host");
    }
    std::tie (user_, found) = configSection.find ("user");
    if (!found)
    {
        throw std::runtime_error (
            "Missing required value in config for postgresql backend: user");
    }
    {
        std::string portAsString;
        std::tie (portAsString, found) = configSection.find ("port");
        if (!found)
        {
            throw std::runtime_error (
                "Missing required value in config for postgresql backend: "
                "user");
        }
        try
        {
            port_ = std::stoi (portAsString);
        }
        catch (...)
        {
            throw std::runtime_error (
                "The port value in the config for the postgresql backend must "
                "be an integer. Got: " +
                portAsString);
        }
    }
}
#endif  // ENABLE_SOCI_POSTGRESQL
}

std::unique_ptr<SociConfig> make_SociConfig (BasicConfig const& config,
                                             std::string dbName)
{
    static const std::string sectionName ("sqdb");
    static const std::string keyName ("backend");
    auto const& section = config.section (sectionName);
    std::string backendName;
    {
        bool found;
        std::tie (backendName, found) = section.find (keyName);
        if (!found)
            backendName = "sqlite";
    }
    if (backendName == "sqlite")
    {
        std::string const path = config.section (SECTION_DATABASE_PATH). legacyValue();
        std::string const ext =
            (dbName == "validators" || dbName == "peerfinder") ? ".sqlite"
                                                               : ".db";
        return std::make_unique<detail::SociSqliteConfig>(
            std::move (dbName), std::move (path), std::move (ext));
    }
#if defined(ENABLE_SOCI_POSTGRESQL) && ENABLE_SOCI_POSTGRESQL
    else if (backendName == "postgresql")
    {
        return std::make_unique<detail::SociPostgresqlConfig>(
            section, std::move (dbName));
    }
#endif
    else
    {
        throw std::runtime_error ("Unsupported soci backend: " + backendName);
    }
}
}
