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

#ifdef RIPPLED_REPORTING
#ifndef RIPPLE_CORE_PG_H_INCLUDED
#define RIPPLE_CORE_PG_H_INCLUDED

#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Stoppable.h>
#include <ripple/protocol/Protocol.h>
#include <boost/lexical_cast.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <libpq-fe.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ripple {

// These postgres structs must be freed only by the postgres API.
using pg_result_type = std::unique_ptr<PGresult, void (*)(PGresult*)>;
using pg_connection_type = std::unique_ptr<PGconn, void (*)(PGconn*)>;

/** first: command
 * second: parameter values
 *
 * The 2nd member takes an optional string to
 * distinguish between NULL parameters and empty strings. An empty
 * item corresponds to a NULL parameter.
 *
 * Postgres reads each parameter as a c-string, regardless of actual type.
 * Binary types (bytea) need to be converted to hex and prepended with
 * \x ("\\x").
 */
using pg_params =
    std::pair<char const*, std::vector<std::optional<std::string>>>;

/** Parameter values for pg API. */
using pg_formatted_params = std::vector<char const*>;

/** Parameters for managing postgres connections. */
struct PgConfig
{
    /** Maximum connections allowed to db. */
    std::size_t max_connections{std::numeric_limits<std::size_t>::max()};
    /** Close idle connections past this duration. */
    std::chrono::seconds timeout{600};

    /** Index of DB connection parameter names. */
    std::vector<char const*> keywordsIdx;
    /** DB connection parameter names. */
    std::vector<std::string> keywords;
    /** Index of DB connection parameter values. */
    std::vector<char const*> valuesIdx;
    /** DB connection parameter values. */
    std::vector<std::string> values;
};

//-----------------------------------------------------------------------------

/** Class that operates on postgres query results.
 *
 * The functions that return results do not check first whether the
 * expected results are actually there. Therefore, the caller first needs
 * to check whether or not a valid response was returned using the operator
 * bool() overload. If number of tuples or fields are unknown, then check
 * those. Each result field should be checked for null before attempting
 * to return results. Finally, the caller must know the type of the field
 * before calling the corresponding function to return a field. Postgres
 * internally stores each result field as null-terminated strings.
 */
class PgResult
{
    // The result object must be freed using the libpq API PQclear() call.
    pg_result_type result_{nullptr, [](PGresult* result) { PQclear(result); }};
    std::optional<std::pair<ExecStatusType, std::string>> error_;

public:
    /** Constructor for when the process is stopping.
     *
     */
    PgResult()
    {
    }

    /** Constructor for successful query results.
     *
     * @param result Query result.
     */
    explicit PgResult(pg_result_type&& result) : result_(std::move(result))
    {
    }

    /** Constructor for failed query results.
     *
     * @param result Query result that contains error information.
     * @param conn Postgres connection that contains error information.
     */
    PgResult(PGresult* result, PGconn* conn)
        : error_({PQresultStatus(result), PQerrorMessage(conn)})
    {
    }

    /** Return field as a null-terminated string pointer.
     *
     * Note that this function does not guarantee that the result struct
     * exists, or that the row and fields exist, or that the field is
     * not null.
     *
     * @param ntuple Row number.
     * @param nfield Field number.
     * @return Field contents.
     */
    char const*
    c_str(int ntuple = 0, int nfield = 0) const
    {
        return PQgetvalue(result_.get(), ntuple, nfield);
    }

    /** Return field as equivalent to Postgres' INT type (32 bit signed).
     *
     * Note that this function does not guarantee that the result struct
     * exists, or that the row and fields exist, or that the field is
     * not null, or that the type is that requested.

     * @param ntuple Row number.
     * @param nfield Field number.
     * @return Field contents.
     */
    std::int32_t
    asInt(int ntuple = 0, int nfield = 0) const
    {
        return boost::lexical_cast<std::int32_t>(
            PQgetvalue(result_.get(), ntuple, nfield));
    }

    /** Return field as equivalent to Postgres' BIGINT type (64 bit signed).
     *
     * Note that this function does not guarantee that the result struct
     * exists, or that the row and fields exist, or that the field is
     * not null, or that the type is that requested.

     * @param ntuple Row number.
     * @param nfield Field number.
     * @return Field contents.
     */
    std::int64_t
    asBigInt(int ntuple = 0, int nfield = 0) const
    {
        return boost::lexical_cast<std::int64_t>(
            PQgetvalue(result_.get(), ntuple, nfield));
    }

    /** Returns whether the field is NULL or not.
     *
     * Note that this function does not guarantee that the result struct
     * exists, or that the row and fields exist.
     *
     * @param ntuple Row number.
     * @param nfield Field number.
     * @return Whether field is NULL.
     */
    bool
    isNull(int ntuple = 0, int nfield = 0) const
    {
        return PQgetisnull(result_.get(), ntuple, nfield);
    }

    /** Check whether a valid response occurred.
     *
     * @return Whether or not the query returned a valid response.
     */
    operator bool() const
    {
        return result_ != nullptr;
    }

    /** Message describing the query results suitable for diagnostics.
     *
     * If error, then the postgres error type and message are returned.
     * Otherwise, "ok"
     *
     * @return Query result message.
     */
    std::string
    msg() const;

    /** Get number of rows in result.
     *
     * Note that this function does not guarantee that the result struct
     * exists.
     *
     * @return Number of result rows.
     */
    int
    ntuples() const
    {
        return PQntuples(result_.get());
    }

    /** Get number of fields in result.
     *
     * Note that this function does not guarantee that the result struct
     * exists.
     *
     * @return Number of result fields.
     */
    int
    nfields() const
    {
        return PQnfields(result_.get());
    }

    /** Return result status of the command.
     *
     * Note that this function does not guarantee that the result struct
     * exists.
     *
     * @return
     */
    ExecStatusType
    status() const
    {
        return PQresultStatus(result_.get());
    }
};

/* Class that contains and operates upon a postgres connection. */
class Pg
{
    friend class PgPool;
    friend class PgQuery;

    PgConfig const& config_;
    beast::Journal const j_;
    bool& stop_;
    std::mutex& mutex_;

    // The connection object must be freed using the libpq API PQfinish() call.
    pg_connection_type conn_{nullptr, [](PGconn* conn) { PQfinish(conn); }};

    /** Clear results from the connection.
     *
     * Results from previous commands must be cleared before new commands
     * can be processed. This function should be called on connections
     * that weren't processed completely before being reused, such as
     * when being checked-in.
     *
     * @return whether or not connection still exists.
     */
    bool
    clear();

    /** Connect to postgres.
     *
     * Idempotently connects to postgres by first checking whether an
     * existing connection is already present. If connection is not present
     * or in an errored state, reconnects to the database.
     */
    void
    connect();

    /** Disconnect from postgres. */
    void
    disconnect()
    {
        conn_.reset();
    }

    /** Execute postgres query.
     *
     * If parameters are included, then the command should contain only a
     * single SQL statement. If no parameters, then multiple SQL statements
     * delimited by semi-colons can be processed. The response is from
     * the last command executed.
     *
     * @param command postgres API command string.
     * @param nParams postgres API number of parameters.
     * @param values postgres API array of parameter.
     * @return Query result object.
     */
    PgResult
    query(char const* command, std::size_t nParams, char const* const* values);

    /** Execute postgres query with no parameters.
     *
     * @param command Query string.
     * @return Query result object;
     */
    PgResult
    query(char const* command)
    {
        return query(command, 0, nullptr);
    }

    /** Execute postgres query with parameters.
     *
     * @param dbParams Database command and parameter values.
     * @return Query result object.
     */
    PgResult
    query(pg_params const& dbParams);

    /** Insert multiple records into a table using Postgres' bulk COPY.
     *
     * Throws upon error.
     *
     * @param table Name of table for import.
     * @param records Records in the COPY IN format.
     */
    void
    bulkInsert(char const* table, std::string const& records);

public:
    /** Constructor for Pg class.
     *
     * @param config Config parameters.
     * @param j Logger object.
     * @param stop Reference to connection pool's stop flag.
     * @param mutex Reference to connection pool's mutex.
     */
    Pg(PgConfig const& config,
       beast::Journal const j,
       bool& stop,
       std::mutex& mutex)
        : config_(config), j_(j), stop_(stop), mutex_(mutex)
    {
    }
};

//-----------------------------------------------------------------------------

/** Database connection pool.
 *
 * Allow re-use of postgres connections. Postgres connections are created
 * as needed until configurable limit is reached. After use, each connection
 * is placed in a container ordered by time of use. Each request for
 * a connection grabs the most recently used connection from the container.
 * If none are available, a new connection is used (up to configured limit).
 * Idle connections are destroyed periodically after configurable
 * timeout duration.
 *
 * This should be stored as a shared pointer so PgQuery objects can safely
 * outlive it.
 */
class PgPool : public Stoppable
{
    friend class PgQuery;

    using clock_type = std::chrono::steady_clock;

    PgConfig config_;
    beast::Journal const j_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::size_t connections_{};
    bool stop_{false};

    /** Idle database connections ordered by timestamp to allow timing out. */
    std::multimap<std::chrono::time_point<clock_type>, std::unique_ptr<Pg>>
        idle_;

    /** Get a postgres connection object.
     *
     * Return the most recent idle connection in the pool, if available.
     * Otherwise, return a new connection unless we're at the threshold.
     * If so, then wait until a connection becomes available.
     *
     * @return Postgres object.
     */
    std::unique_ptr<Pg>
    checkout();

    /** Return a postgres object to the pool for reuse.
     *
     * If connection is healthy, place in pool for reuse. After calling this,
     * the container no longer have a connection unless checkout() is called.
     *
     * @param pg Pg object.
     */
    void
    checkin(std::unique_ptr<Pg>& pg);

public:
    /** Connection pool constructor.
     *
     * @param pgConfig Postgres config.
     * @param j Logger object.
     * @param parent Stoppable parent.
     */
    PgPool(Section const& pgConfig, Stoppable& parent, beast::Journal j);

    /** Initiate idle connection timer.
     *
     * The PgPool object needs to be fully constructed to support asynchronous
     * operations.
     */
    void
    setup();

    /** Prepare for process shutdown. (Stoppable) */
    void
    onStop() override;

    /** Disconnect idle postgres connections. */
    void
    idleSweeper();
};

//-----------------------------------------------------------------------------

/** Class to query postgres.
 *
 * This class should be used by functions outside of this
 * compilation unit for querying postgres. It automatically acquires and
 * relinquishes a database connection to handle each query.
 */
class PgQuery
{
private:
    std::shared_ptr<PgPool> pool_;
    std::unique_ptr<Pg> pg_;

public:
    PgQuery() = delete;

    PgQuery(std::shared_ptr<PgPool> const& pool)
        : pool_(pool), pg_(pool->checkout())
    {
    }

    ~PgQuery()
    {
        pool_->checkin(pg_);
    }

    /** Execute postgres query with parameters.
     *
     * @param dbParams Database command with parameters.
     * @return Result of query, including errors.
     */
    PgResult
    operator()(pg_params const& dbParams)
    {
        if (!pg_)  // It means we're stopping. Return empty result.
            return PgResult();
        return pg_->query(dbParams);
    }

    /** Execute postgres query with only command statement.
     *
     * @param command Command statement.
     * @return Result of query, including errors.
     */
    PgResult
    operator()(char const* command)
    {
        return operator()(pg_params{command, {}});
    }

    /** Insert multiple records into a table using Postgres' bulk COPY.
     *
     * Throws upon error.
     *
     * @param table Name of table for import.
     * @param records Records in the COPY IN format.
     */
    void
    bulkInsert(char const* table, std::string const& records)
    {
        pg_->bulkInsert(table, records);
    }
};

//-----------------------------------------------------------------------------

/** Create Postgres connection pool manager.
 *
 * @param pgConfig Configuration for Postgres.
 * @param j Logger object.
 * @param parent Stoppable parent object.
 * @return Postgres connection pool manager
 */
std::shared_ptr<PgPool>
make_PgPool(Section const& pgConfig, Stoppable& parent, beast::Journal j);

/** Initialize the Postgres schema.
 *
 * This function ensures that the database is running the latest version
 * of the schema.
 *
 * @param pool Postgres connection pool manager.
 */
void
initSchema(std::shared_ptr<PgPool> const& pool);

}  // namespace ripple

#endif  // RIPPLE_CORE_PG_H_INCLUDED
#endif  // RIPPLED_REPORTING
