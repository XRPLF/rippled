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

#include <soci/soci-backend.h>

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

    void define_by_pos(int& position, void* data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch, indicator* ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    empty_statement_backend& statement_;
};

struct SOCI_EMPTY_DECL empty_vector_into_type_backend : details::vector_into_type_backend
{
    empty_vector_into_type_backend(empty_statement_backend &st)
        : statement_(st)
    {}

    void define_by_pos(int& position, void* data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator* ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    empty_statement_backend& statement_;
};

struct SOCI_EMPTY_DECL empty_standard_use_type_backend : details::standard_use_type_backend
{
    empty_standard_use_type_backend(empty_statement_backend &st)
        : statement_(st)
    {}

    void bind_by_pos(int& position, void* data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const& name, void* data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    void pre_use(indicator const* ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator* ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    empty_statement_backend& statement_;
};

struct SOCI_EMPTY_DECL empty_vector_use_type_backend : details::vector_use_type_backend
{
    empty_vector_use_type_backend(empty_statement_backend &st)
        : statement_(st) {}

    void bind_by_pos(int& position, void* data, details::exchange_type type) SOCI_OVERRIDE;
    void bind_by_name(std::string const& name, void* data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_use(indicator const* ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    empty_statement_backend& statement_;
};

struct empty_session_backend;
struct SOCI_EMPTY_DECL empty_statement_backend : details::statement_backend
{
    empty_statement_backend(empty_session_backend &session);

    void alloc() SOCI_OVERRIDE;
    void clean_up() SOCI_OVERRIDE;
    void prepare(std::string const& query, details::statement_type eType) SOCI_OVERRIDE;

    exec_fetch_result execute(int number) SOCI_OVERRIDE;
    exec_fetch_result fetch(int number) SOCI_OVERRIDE;

    long long get_affected_rows() SOCI_OVERRIDE;
    int get_number_of_rows() SOCI_OVERRIDE;
    std::string get_parameter_name(int index) const SOCI_OVERRIDE;

    std::string rewrite_for_procedure_call(std::string const& query) SOCI_OVERRIDE;

    int prepare_for_describe() SOCI_OVERRIDE;
    void describe_column(int colNum, data_type& dtype, std::string& columnName) SOCI_OVERRIDE;

    empty_standard_into_type_backend* make_into_type_backend() SOCI_OVERRIDE;
    empty_standard_use_type_backend* make_use_type_backend() SOCI_OVERRIDE;
    empty_vector_into_type_backend* make_vector_into_type_backend() SOCI_OVERRIDE;
    empty_vector_use_type_backend* make_vector_use_type_backend() SOCI_OVERRIDE;

    empty_session_backend& session_;
};

struct empty_rowid_backend : details::rowid_backend
{
    empty_rowid_backend(empty_session_backend &session);

    ~empty_rowid_backend() SOCI_OVERRIDE;
};

struct empty_blob_backend : details::blob_backend
{
    empty_blob_backend(empty_session_backend& session);

    ~empty_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;
    std::size_t read(std::size_t offset, char* buf, std::size_t toRead) SOCI_OVERRIDE;
    std::size_t write(std::size_t offset, char const* buf, std::size_t toWrite) SOCI_OVERRIDE;
    std::size_t append(char const* buf, std::size_t toWrite) SOCI_OVERRIDE;
    void trim(std::size_t newLen) SOCI_OVERRIDE;

    empty_session_backend& session_;
};

struct empty_session_backend : details::session_backend
{
    empty_session_backend(connection_parameters const& parameters);

    ~empty_session_backend() SOCI_OVERRIDE;

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    std::string get_dummy_from_table() const SOCI_OVERRIDE { return std::string(); }

    std::string get_backend_name() const SOCI_OVERRIDE { return "empty"; }

    void clean_up();

    empty_statement_backend* make_statement_backend() SOCI_OVERRIDE;
    empty_rowid_backend* make_rowid_backend() SOCI_OVERRIDE;
    empty_blob_backend* make_blob_backend() SOCI_OVERRIDE;
};

struct SOCI_EMPTY_DECL empty_backend_factory : backend_factory
{
    empty_backend_factory() {}
    empty_session_backend* make_session(connection_parameters const& parameters) const SOCI_OVERRIDE;
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
