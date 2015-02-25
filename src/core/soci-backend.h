//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BACKEND_H_INCLUDED
#define SOCI_BACKEND_H_INCLUDED

#include "soci-config.h"
#include "error.h"
// std
#include <cstddef>
#include <map>
#include <string>

namespace soci
{

// data types, as seen by the user
enum data_type
{
    dt_string, dt_date, dt_double, dt_integer, dt_long_long, dt_unsigned_long_long
};

// the enum type for indicator variables
enum indicator { i_ok, i_null, i_truncated };

class session;

namespace details
{

// data types, as used to describe exchange format
enum exchange_type
{
    x_char,
    x_stdstring,
    x_short,
    x_integer,
    x_long_long,
    x_unsigned_long_long,
    x_double,
    x_stdtm,
    x_statement,
    x_rowid,
    x_blob
};

// type of statement (used for optimizing statement preparation)
enum statement_type
{
    st_one_time_query,
    st_repeatable_query
};

// polymorphic into type backend

class standard_into_type_backend
{
public:
    standard_into_type_backend() {}
    virtual ~standard_into_type_backend() {}

    virtual void define_by_pos(int& position, void* data, exchange_type type) = 0;

    virtual void pre_fetch() = 0;
    virtual void post_fetch(bool gotData, bool calledFromFetch, indicator* ind) = 0;

    virtual void clean_up() = 0;

private:
    // noncopyable
    standard_into_type_backend(standard_into_type_backend const&);
    standard_into_type_backend& operator=(standard_into_type_backend const&);
};

class vector_into_type_backend
{
public:

    vector_into_type_backend() {}
    virtual ~vector_into_type_backend() {}

    virtual void define_by_pos(int& position, void* data, exchange_type type) = 0;

    virtual void pre_fetch() = 0;
    virtual void post_fetch(bool gotData, indicator* ind) = 0;

    virtual void resize(std::size_t sz) = 0;
    virtual std::size_t size() = 0;

    virtual void clean_up() = 0;

private:
    // noncopyable
    vector_into_type_backend(vector_into_type_backend const&);
    vector_into_type_backend& operator=(vector_into_type_backend const&);
};

// polymorphic use type backend

class standard_use_type_backend
{
public:
    standard_use_type_backend() {}
    virtual ~standard_use_type_backend() {}

    virtual void bind_by_pos(int& position, void* data,
        exchange_type type, bool readOnly) = 0;
    virtual void bind_by_name(std::string const& name,
        void* data, exchange_type type, bool readOnly) = 0;

    virtual void pre_use(indicator const* ind) = 0;
    virtual void post_use(bool gotData, indicator * ind) = 0;

    virtual void clean_up() = 0;

private:
    // noncopyable
    standard_use_type_backend(standard_use_type_backend const&);
    standard_use_type_backend& operator=(standard_use_type_backend const&);
};

class vector_use_type_backend
{
public:
    vector_use_type_backend() {}
    virtual ~vector_use_type_backend() {}

    virtual void bind_by_pos(int& position, void* data, exchange_type type) = 0;
    virtual void bind_by_name(std::string const& name,
        void* data, exchange_type type) = 0;

    virtual void pre_use(indicator const* ind) = 0;

    virtual std::size_t size() = 0;

    virtual void clean_up() = 0;

private:
    // noncopyable
    vector_use_type_backend(vector_use_type_backend const&);
    vector_use_type_backend& operator=(vector_use_type_backend const&);
};

// polymorphic statement backend

class statement_backend
{
public:
    statement_backend() {}
    virtual ~statement_backend() {}

    virtual void alloc() = 0;
    virtual void clean_up() = 0;

    virtual void prepare(std::string const& query, statement_type eType) = 0;

    enum exec_fetch_result
    {
        ef_success,
        ef_no_data
    };

    virtual exec_fetch_result execute(int number) = 0;
    virtual exec_fetch_result fetch(int number) = 0;

    virtual long long get_affected_rows() = 0;
    virtual int get_number_of_rows() = 0;

    virtual std::string rewrite_for_procedure_call(std::string const& query) = 0;

    virtual int prepare_for_describe() = 0;
    virtual void describe_column(int colNum, data_type& dtype,
        std::string& column_name) = 0;

    virtual standard_into_type_backend* make_into_type_backend() = 0;
    virtual standard_use_type_backend* make_use_type_backend() = 0;
    virtual vector_into_type_backend* make_vector_into_type_backend() = 0;
    virtual vector_use_type_backend* make_vector_use_type_backend() = 0;

private:
    // noncopyable
    statement_backend(statement_backend const&);
    statement_backend& operator=(statement_backend const&);
};

// polymorphic RowID backend

class rowid_backend
{
public:
    virtual ~rowid_backend() {}
};

// polymorphic blob backend

class blob_backend
{
public:
    blob_backend() {}
    virtual ~blob_backend() {}

    virtual std::size_t get_len() = 0;
    virtual std::size_t read(std::size_t offset, char* buf,
        std::size_t toRead) = 0;
    virtual std::size_t write(std::size_t offset, char const* buf,
        std::size_t toWrite) = 0;
    virtual std::size_t append(char const* buf, std::size_t toWrite) = 0;
    virtual void trim(std::size_t newLen) = 0;

private:
    // noncopyable
    blob_backend(blob_backend const&);
    blob_backend& operator=(blob_backend const&);
};

// polymorphic session backend

class session_backend
{
public:
    session_backend() {}
    virtual ~session_backend() {}

    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    // At least one of these functions is usually not implemented for any given
    // backend as RDBMS support either sequences or auto-generated values, so
    // we don't declare them as pure virtuals to avoid having to define trivial
    // versions of them in the derived classes. However every backend should
    // define at least one of them to allow the code using auto-generated values
    // to work.
    virtual bool get_next_sequence_value(session&, std::string const&, long&)
    {
        return false;
    }
    virtual bool get_last_insert_id(session&, std::string const&, long&)
    {
        return false;
    }

    virtual std::string get_backend_name() const = 0;

    virtual statement_backend* make_statement_backend() = 0;
    virtual rowid_backend* make_rowid_backend() = 0;
    virtual blob_backend* make_blob_backend() = 0;

private:
    // noncopyable
    session_backend(session_backend const&);
    session_backend& operator=(session_backend const&);
};

} // namespace details

// simple base class for the session back-end factory

class connection_parameters;

class SOCI_DECL backend_factory
{
public:
    backend_factory() {}
    virtual ~backend_factory() {}

    virtual details::session_backend* make_session(
        connection_parameters const& parameters) const = 0;
};

} // namespace soci

#endif // SOCI_BACKEND_H_INCLUDED
