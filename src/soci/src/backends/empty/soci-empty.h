//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_EMPTY_H_INCLUDED
#define SOCI_EMPTY_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_EMPTY_SOURCE
#   define SOCI_EMPTY_DECL __declspec(dllexport)
#  else
#   define SOCI_EMPTY_DECL __declspec(dllimport)
#  endif // SOCI_EMPTY_SOURCE
# endif // SOCI_DLL
#endif // _WIN32
//
// If SOCI_EMPTY_DECL isn't defined yet define it now
#ifndef SOCI_EMPTY_DECL
# define SOCI_EMPTY_DECL
#endif

#include "soci-backend.h"

#include <cstddef>
#include <string>

namespace soci
{

struct empty_statement_backend;

struct SOCI_EMPTY_DECL empty_standard_into_type_backend : details::standard_into_type_backend
{
    empty_standard_into_type_backend(empty_statement_backend &st)
        : statement_(st)
    {}

    void define_by_pos(int& position, void* data, details::exchange_type type);

    void pre_fetch();
    void post_fetch(bool gotData, bool calledFromFetch, indicator* ind);

    void clean_up();

    empty_statement_backend& statement_;
};

struct SOCI_EMPTY_DECL empty_vector_into_type_backend : details::vector_into_type_backend
{
    empty_vector_into_type_backend(empty_statement_backend &st)
        : statement_(st)
    {}

    void define_by_pos(int& position, void* data, details::exchange_type type);

    void pre_fetch();
    void post_fetch(bool gotData, indicator* ind);

    void resize(std::size_t sz);
    std::size_t size();

    void clean_up();

    empty_statement_backend& statement_;
};

struct SOCI_EMPTY_DECL empty_standard_use_type_backend : details::standard_use_type_backend
{
    empty_standard_use_type_backend(empty_statement_backend &st)
        : statement_(st)
    {}

    void bind_by_pos(int& position, void* data, details::exchange_type type, bool readOnly);
    void bind_by_name(std::string const& name, void* data, details::exchange_type type, bool readOnly);

    void pre_use(indicator const* ind);
    void post_use(bool gotData, indicator* ind);

    void clean_up();

    empty_statement_backend& statement_;
};

struct SOCI_EMPTY_DECL empty_vector_use_type_backend : details::vector_use_type_backend
{
    empty_vector_use_type_backend(empty_statement_backend &st)
        : statement_(st) {}

    void bind_by_pos(int& position, void* data, details::exchange_type type);
    void bind_by_name(std::string const& name, void* data, details::exchange_type type);

    void pre_use(indicator const* ind);

    std::size_t size();

    void clean_up();

    empty_statement_backend& statement_;
};

struct empty_session_backend;
struct SOCI_EMPTY_DECL empty_statement_backend : details::statement_backend
{
    empty_statement_backend(empty_session_backend &session);

    void alloc();
    void clean_up();
    void prepare(std::string const& query, details::statement_type eType);

    exec_fetch_result execute(int number);
    exec_fetch_result fetch(int number);

    long long get_affected_rows();
    int get_number_of_rows();

    std::string rewrite_for_procedure_call(std::string const& query);

    int prepare_for_describe();
    void describe_column(int colNum, data_type& dtype, std::string& columnName);

    empty_standard_into_type_backend* make_into_type_backend();
    empty_standard_use_type_backend* make_use_type_backend();
    empty_vector_into_type_backend* make_vector_into_type_backend();
    empty_vector_use_type_backend* make_vector_use_type_backend();

    empty_session_backend& session_;
};

struct empty_rowid_backend : details::rowid_backend
{
    empty_rowid_backend(empty_session_backend &session);

    ~empty_rowid_backend();
};

struct empty_blob_backend : details::blob_backend
{
    empty_blob_backend(empty_session_backend& session);

    ~empty_blob_backend();

    std::size_t get_len();
    std::size_t read(std::size_t offset, char* buf, std::size_t toRead);
    std::size_t write(std::size_t offset, char const* buf, std::size_t toWrite);
    std::size_t append(char const* buf, std::size_t toWrite);
    void trim(std::size_t newLen);

    empty_session_backend& session_;
};

struct empty_session_backend : details::session_backend
{
    empty_session_backend(connection_parameters const& parameters);

    ~empty_session_backend();

    void begin();
    void commit();
    void rollback();

    std::string get_backend_name() const { return "empty"; }

    void clean_up();

    empty_statement_backend* make_statement_backend();
    empty_rowid_backend* make_rowid_backend();
    empty_blob_backend* make_blob_backend();
};

struct SOCI_EMPTY_DECL empty_backend_factory : backend_factory
{
    empty_backend_factory() {}
    empty_session_backend* make_session(connection_parameters const& parameters) const;
};

extern SOCI_EMPTY_DECL empty_backend_factory const empty;

extern "C"
{

// for dynamic backend loading
SOCI_EMPTY_DECL backend_factory const* factory_empty();
SOCI_EMPTY_DECL void register_factory_empty();

} // extern "C"

} // namespace soci

#endif // SOCI_EMPTY_H_INCLUDED
