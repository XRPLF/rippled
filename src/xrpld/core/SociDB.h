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

#ifndef RIPPLE_SOCIDB_H_INCLUDED
#define RIPPLE_SOCIDB_H_INCLUDED

/** An embedded database wrapper with an intuitive, type-safe interface.

    This collection of classes let's you access embedded SQLite databases
    using C++ syntax that is very similar to regular SQL.

    This module requires the @ref beast_sqlite external module.
*/

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
#endif

#include <xrpld/core/JobQueue.h>

#include <xrpl/basics/Log.h>

#define SOCI_USE_BOOST
#include <soci/soci.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sqlite_api {
struct sqlite3;
}

namespace ripple {

class BasicConfig;

/**
   DBConfig is used when a client wants to delay opening a soci::session after
   parsing the config parameters. If a client want to open a session
   immediately, use the free function "open" below.
 */
class DBConfig
{
    std::string connectionString_;
    explicit DBConfig(std::string const& dbPath);

public:
    DBConfig(BasicConfig const& config, std::string const& dbName);
    std::string
    connectionString() const;
    void
    open(soci::session& s) const;
};

/**
   Open a soci session.

   @param s Session to open.

   @param config Parameters to pick the soci backend and how to connect to that
                 backend.

   @param dbName Name of the database. This has different meaning for different
                 backends. Sometimes it is part of a filename (sqlite3),
                 other times it is a database name (postgresql).
*/
void
open(soci::session& s, BasicConfig const& config, std::string const& dbName);

/**
 *  Open a soci session.
 *
 *  @param s Session to open.
 *  @param beName Backend name.
 *  @param connectionString Connection string to forward to soci::open.
 *         see the soci::open documentation for how to use this.
 *
 */
void
open(
    soci::session& s,
    std::string const& beName,
    std::string const& connectionString);

std::uint32_t
getKBUsedAll(soci::session& s);
std::uint32_t
getKBUsedDB(soci::session& s);

void
convert(soci::blob& from, std::vector<std::uint8_t>& to);
void
convert(soci::blob& from, std::string& to);
void
convert(std::vector<std::uint8_t> const& from, soci::blob& to);
void
convert(std::string const& from, soci::blob& to);

class Checkpointer : public std::enable_shared_from_this<Checkpointer>
{
public:
    virtual std::uintptr_t
    id() const = 0;
    virtual ~Checkpointer() = default;

    virtual void
    schedule() = 0;

    virtual void
    checkpoint() = 0;
};

/** Returns a new checkpointer which makes checkpoints of a
    soci database every checkpointPageCount pages, using a job on the job queue.

    The checkpointer contains references to the session and job queue
    and so must outlive them both.
 */
std::shared_ptr<Checkpointer>
makeCheckpointer(
    std::uintptr_t id,
    std::weak_ptr<soci::session>,
    JobQueue&,
    Logs&);

}  // namespace ripple

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
