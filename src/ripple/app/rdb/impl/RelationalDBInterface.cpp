//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DatabaseShard.h>

namespace ripple {

extern std::unique_ptr<RelationalDBInterface>
getRelationalDBInterfaceSqlite(
    Application& app,
    Config const& config,
    JobQueue& jobQueue);

extern std::unique_ptr<RelationalDBInterface>
getRelationalDBInterfacePostgres(
    Application& app,
    Config const& config,
    JobQueue& jobQueue);

std::unique_ptr<RelationalDBInterface>
RelationalDBInterface::init(
    Application& app,
    Config const& config,
    JobQueue& jobQueue)
{
    bool use_sqlite = false;
    bool use_postgres = false;

    if (config.reporting())
    {
        use_postgres = true;
    }
    else
    {
        const Section& rdb_section{config.section(SECTION_RELATIONAL_DB)};
        if (!rdb_section.empty())
        {
            if (boost::iequals(get(rdb_section, "backend"), "sqlite"))
            {
                use_sqlite = true;
            }
            else
            {
                Throw<std::runtime_error>(
                    "Invalid rdb_section backend value: " +
                    get(rdb_section, "backend"));
            }
        }
        else
        {
            use_sqlite = true;
        }
    }

    if (use_sqlite)
    {
        return getRelationalDBInterfaceSqlite(app, config, jobQueue);
    }
    else if (use_postgres)
    {
        return getRelationalDBInterfacePostgres(app, config, jobQueue);
    }

    return std::unique_ptr<RelationalDBInterface>();
}

}  // namespace ripple
