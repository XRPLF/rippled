//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "session.h"
#include "connection-parameters.h"
#include "connection-pool.h"
#include "soci-backend.h"
#include "query_transformation.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;

namespace // anonymous
{

void ensureConnected(session_backend * backEnd)
{
    if (backEnd == NULL)
    {
        throw soci_error("Session is not connected.");
    }
}

} // namespace anonymous

session::session()
    : once(this), prepare(this), query_transformation_(NULL), logStream_(NULL),
      uppercaseColumnNames_(false), backEnd_(NULL),
      isFromPool_(false), pool_(NULL)
{
}

session::session(connection_parameters const & parameters)
    : once(this), prepare(this), query_transformation_(NULL), logStream_(NULL),
      lastConnectParameters_(parameters),
      uppercaseColumnNames_(false), backEnd_(NULL),
      isFromPool_(false), pool_(NULL)
{
    open(lastConnectParameters_);
}

session::session(backend_factory const & factory,
    std::string const & connectString)
    : once(this), prepare(this), query_transformation_(NULL), logStream_(NULL),
      lastConnectParameters_(factory, connectString),
      uppercaseColumnNames_(false), backEnd_(NULL),
      isFromPool_(false), pool_(NULL)
{
    open(lastConnectParameters_);
}

session::session(std::string const & backendName,
    std::string const & connectString)
    : once(this), prepare(this), query_transformation_(NULL), logStream_(NULL),
      lastConnectParameters_(backendName, connectString),
      uppercaseColumnNames_(false), backEnd_(NULL),
      isFromPool_(false), pool_(NULL)
{
    open(lastConnectParameters_);
}

session::session(std::string const & connectString)
    : once(this), prepare(this), query_transformation_(NULL), logStream_(NULL),
      lastConnectParameters_(connectString),
      uppercaseColumnNames_(false), backEnd_(NULL),
      isFromPool_(false), pool_(NULL)
{
    open(lastConnectParameters_);
}

session::session(connection_pool & pool)
    : query_transformation_(NULL), logStream_(NULL), isFromPool_(true), pool_(&pool)
{
    poolPosition_ = pool.lease();
    session & pooledSession = pool.at(poolPosition_);

    once.set_session(&pooledSession);
    prepare.set_session(&pooledSession);
    backEnd_ = pooledSession.get_backend();
}

session::~session()
{
    if (isFromPool_)
    {
        pool_->give_back(poolPosition_);
    }
    else
    {
        delete query_transformation_;
        delete backEnd_;
    }
}

void session::open(connection_parameters const & parameters)
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).open(parameters);
    }
    else
    {
        if (backEnd_ != NULL)
        {
            throw soci_error("Cannot open already connected session.");
        }

        backend_factory const * const factory = parameters.get_factory();
        if (factory == NULL)
        {
            throw soci_error("Cannot connect without a valid backend.");
        }

        backEnd_ = factory->make_session(parameters);
        lastConnectParameters_ = parameters;
    }
}

void session::open(backend_factory const & factory,
    std::string const & connectString)
{
    open(connection_parameters(factory, connectString));
}

void session::open(std::string const & backendName,
    std::string const & connectString)
{
    open(connection_parameters(backendName, connectString));
}

void session::open(std::string const & connectString)
{
    open(connection_parameters(connectString));
}

void session::close()
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).close();
        backEnd_ = NULL;
    }
    else
    {
        delete backEnd_;
        backEnd_ = NULL;
    }
}

void session::reconnect()
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).reconnect();
        backEnd_ = pool_->at(poolPosition_).get_backend();
    }
    else
    {
        backend_factory const * const lastFactory = lastConnectParameters_.get_factory();
        if (lastFactory == NULL)
        {
            throw soci_error("Cannot reconnect without previous connection.");
        }

        if (backEnd_ != NULL)
        {
            close();
        }

        backEnd_ = lastFactory->make_session(lastConnectParameters_);
    }
}

void session::begin()
{
    ensureConnected(backEnd_);

    backEnd_->begin();
}

void session::commit()
{
    ensureConnected(backEnd_);

    backEnd_->commit();
}

void session::rollback()
{
    ensureConnected(backEnd_);

    backEnd_->rollback();
}

std::ostringstream & session::get_query_stream()
{
    if (isFromPool_)
    {
        return pool_->at(poolPosition_).get_query_stream();
    }
    else
    {
        return query_stream_;
    }
}

std::string session::get_query() const
{
    if (isFromPool_)
    {
        return pool_->at(poolPosition_).get_query();
    }
    else
    {
        // preserve logical constness of get_query,
        // stream used as read-only here, 
        session* pthis = const_cast<session*>(this);

        // sole place where any user-defined query transformation is applied
        if (query_transformation_)
        {
            return (*query_transformation_)(pthis->get_query_stream().str());
        }
        return pthis->get_query_stream().str();
    }
}

void session::set_query_transformation_(
        std::auto_ptr<details::query_transformation_function> qtf)
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).set_query_transformation_(qtf);
    }
    else
    {
        delete query_transformation_;
        query_transformation_= qtf.release();
    }
}

void session::set_log_stream(std::ostream * s)
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).set_log_stream(s);
    }
    else
    {
        logStream_ = s;
    }
}

std::ostream * session::get_log_stream() const
{
    if (isFromPool_)
    {
        return pool_->at(poolPosition_).get_log_stream();
    }
    else
    {
        return logStream_;
    }
}

void session::log_query(std::string const & query)
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).log_query(query);
    }
    else
    {
        if (logStream_ != NULL)
        {
            *logStream_ << query << '\n';
        }

        lastQuery_ = query;
    }
}

std::string session::get_last_query() const
{
    if (isFromPool_)
    {
        return pool_->at(poolPosition_).get_last_query();
    }
    else
    {
        return lastQuery_;
    }
}

void session::set_got_data(bool gotData)
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).set_got_data(gotData);
    }
    else
    {
        gotData_ = gotData;
    }
}

bool session::got_data() const
{
    if (isFromPool_)
    {
        return pool_->at(poolPosition_).got_data();
    }
    else
    {
        return gotData_;
    }
}

void session::uppercase_column_names(bool forceToUpper)
{
    if (isFromPool_)
    {
        pool_->at(poolPosition_).uppercase_column_names(forceToUpper);
    }
    else
    {
        uppercaseColumnNames_ = forceToUpper;
    }
}

bool session::get_uppercase_column_names() const
{
    if (isFromPool_)
    {
        return pool_->at(poolPosition_).get_uppercase_column_names();
    }
    else
    {
        return uppercaseColumnNames_;
    }
}

bool session::get_next_sequence_value(std::string const & sequence, long & value)
{
    ensureConnected(backEnd_);

    return backEnd_->get_next_sequence_value(*this, sequence, value);
}

bool session::get_last_insert_id(std::string const & sequence, long & value)
{
    ensureConnected(backEnd_);

    return backEnd_->get_last_insert_id(*this, sequence, value);
}

std::string session::get_backend_name() const
{
    ensureConnected(backEnd_);

    return backEnd_->get_backend_name();
}

statement_backend * session::make_statement_backend()
{
    ensureConnected(backEnd_);

    return backEnd_->make_statement_backend();
}

rowid_backend * session::make_rowid_backend()
{
    ensureConnected(backEnd_);

    return backEnd_->make_rowid_backend();
}

blob_backend * session::make_blob_backend()
{
    ensureConnected(backEnd_);

    return backEnd_->make_blob_backend();
}
