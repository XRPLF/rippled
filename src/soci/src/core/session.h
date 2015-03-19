//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_SESSION_H_INCLUDED
#define SOCI_SESSION_H_INCLUDED

#include "once-temp-type.h"
#include "query_transformation.h"
#include "connection-parameters.h"

// std
#include <cstddef>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

namespace soci
{
class values;
class backend_factory;

namespace details
{

class session_backend;
class statement_backend;
class rowid_backend;
class blob_backend;

} // namespace details

class connection_pool;

class SOCI_DECL session
{
private:

    void set_query_transformation_(std::auto_ptr<details::query_transformation_function> qtf);

public:
    session();
    explicit session(connection_parameters const & parameters);
    session(backend_factory const & factory, std::string const & connectString);
    session(std::string const & backendName, std::string const & connectString);
    explicit session(std::string const & connectString);
    explicit session(connection_pool & pool);

    ~session();

    void open(connection_parameters const & parameters);
    void open(backend_factory const & factory, std::string const & connectString);
    void open(std::string const & backendName, std::string const & connectString);
    void open(std::string const & connectString);
    void close();
    void reconnect();

    void begin();
    void commit();
    void rollback();

    // once and prepare are for syntax sugar only
    details::once_type once;
    details::prepare_type prepare;

    // even more sugar
    template <typename T>
    details::once_temp_type operator<<(T const & t) { return once << t; }

    std::ostringstream & get_query_stream();
    std::string get_query() const;

    template <typename T>
    void set_query_transformation(T callback)
    {
        std::auto_ptr<details::query_transformation_function> qtf(new details::query_transformation<T>(callback));
        set_query_transformation_(qtf);

        assert(qtf.get() == NULL);
    }

    // support for basic logging
    void set_log_stream(std::ostream * s);
    std::ostream * get_log_stream() const;

    void log_query(std::string const & query);
    std::string get_last_query() const;

    void set_got_data(bool gotData);
    bool got_data() const;

    void uppercase_column_names(bool forceToUpper);

    bool get_uppercase_column_names() const;

    // Functions for dealing with sequence/auto-increment values.

    // If true is returned, value is filled with the next value from the given
    // sequence. Otherwise either the sequence is invalid (doesn't exist) or
    // the current backend doesn't support sequences. If you use sequences for
    // automatically generating primary key values, you should use
    // get_last_insert_id() after the insertion in this case.
    bool get_next_sequence_value(std::string const & sequence, long & value);

    // If true is returned, value is filled with the last auto-generated value
    // for this table (although some backends ignore the table argument and
    // return the last value auto-generated in this session).
    bool get_last_insert_id(std::string const & table, long & value);


    // for diagnostics and advanced users
    // (downcast it to expected back-end session class)
    details::session_backend * get_backend() { return backEnd_; }

    std::string get_backend_name() const;

    details::statement_backend * make_statement_backend();
    details::rowid_backend * make_rowid_backend();
    details::blob_backend * make_blob_backend();

private:
    session(session const &);
    session& operator=(session const &);

    std::ostringstream query_stream_;
    details::query_transformation_function* query_transformation_;

    std::ostream * logStream_;
    std::string lastQuery_;

    connection_parameters lastConnectParameters_;

    bool uppercaseColumnNames_;

    details::session_backend * backEnd_;

    bool gotData_;

    bool isFromPool_;
    std::size_t poolPosition_;
    connection_pool * pool_;
};

} // namespace soci

#endif // SOCI_SESSION_H_INCLUDED
