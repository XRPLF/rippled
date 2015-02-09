//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_SQLITE3_H_INCLUDED
#define SOCI_SQLITE3_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_SQLITE3_SOURCE
#   define SOCI_SQLITE3_DECL __declspec(dllexport)
#  else
#   define SOCI_SQLITE3_DECL __declspec(dllimport)
#  endif // SOCI_SQLITE3_SOURCE
# endif // SOCI_DLL
#endif // _WIN32
//
// If SOCI_SQLITE3_DECL isn't defined yet define it now
#ifndef SOCI_SQLITE3_DECL
# define SOCI_SQLITE3_DECL
#endif

#include <cstdarg>
#include <vector>
#include "soci-backend.h"

// Disable flood of nonsense warnings generated for SQLite
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4510 4512 4610)
#endif

namespace sqlite_api
{

#if SQLITE_VERSION_NUMBER < 3003010
// The sqlite3_destructor_type typedef introduced in 3.3.10
// http://www.sqlite.org/cvstrac/tktview?tn=2191
typedef void (*sqlite3_destructor_type)(void*);
#endif

#include <sqlite3.h>

} // namespace sqlite_api

#undef SQLITE_STATIC
#define SQLITE_STATIC ((sqlite_api::sqlite3_destructor_type)0)

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace soci
{

struct sqlite3_statement_backend;
struct sqlite3_standard_into_type_backend : details::standard_into_type_backend
{
    sqlite3_standard_into_type_backend(sqlite3_statement_backend &st)
        : statement_(st) {}

    virtual void define_by_pos(int &position,
                             void *data, details::exchange_type type);

    virtual void pre_fetch();
    virtual void post_fetch(bool gotData, bool calledFromFetch,
                           indicator *ind);

    virtual void clean_up();

    sqlite3_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
};

struct sqlite3_vector_into_type_backend : details::vector_into_type_backend
{
    sqlite3_vector_into_type_backend(sqlite3_statement_backend &st)
        : statement_(st) {}

    void define_by_pos(int& position, void* data, details::exchange_type type);

    void pre_fetch();
    void post_fetch(bool gotData, indicator* ind);

    void resize(std::size_t sz);
    std::size_t size();

    virtual void clean_up();

    sqlite3_statement_backend& statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
};

struct sqlite3_standard_use_type_backend : details::standard_use_type_backend
{
    sqlite3_standard_use_type_backend(sqlite3_statement_backend &st)
        : statement_(st), buf_(0) {}

    virtual void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly);
    virtual void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly);

    virtual void pre_use(indicator const *ind);
    virtual void post_use(bool gotData, indicator *ind);

    virtual void clean_up();

    sqlite3_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
    std::string name_;
    char *buf_;
};

struct sqlite3_vector_use_type_backend : details::vector_use_type_backend
{
    sqlite3_vector_use_type_backend(sqlite3_statement_backend &st)
        : statement_(st) {}

    virtual void bind_by_pos(int &position,
                           void *data, details::exchange_type type);
    virtual void bind_by_name(std::string const &name,
                            void *data, details::exchange_type type);

    virtual void pre_use(indicator const *ind);

    virtual std::size_t size();

    virtual void clean_up();

    sqlite3_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
    std::string name_;
};

struct sqlite3_column
{
    std::string data_;
    bool isNull_;
    char * blobBuf_;
    std::size_t blobSize_;
};

typedef std::vector<sqlite3_column> sqlite3_row;
typedef std::vector<sqlite3_row> sqlite3_recordset;

struct sqlite3_session_backend;
struct sqlite3_statement_backend : details::statement_backend
{
    sqlite3_statement_backend(sqlite3_session_backend &session);

    virtual void alloc();
    virtual void clean_up();
    virtual void prepare(std::string const &query,
        details::statement_type eType);
    void reset_if_needed();

    virtual exec_fetch_result execute(int number);
    virtual exec_fetch_result fetch(int number);

    virtual long long get_affected_rows();
    virtual int get_number_of_rows();

    virtual std::string rewrite_for_procedure_call(std::string const &query);

    virtual int prepare_for_describe();
    virtual void describe_column(int colNum, data_type &dtype,
                                std::string &columnName);

    virtual sqlite3_standard_into_type_backend * make_into_type_backend();
    virtual sqlite3_standard_use_type_backend * make_use_type_backend();
    virtual sqlite3_vector_into_type_backend * make_vector_into_type_backend();
    virtual sqlite3_vector_use_type_backend * make_vector_use_type_backend();

    sqlite3_session_backend &session_;
    sqlite_api::sqlite3_stmt *stmt_;
    sqlite3_recordset dataCache_;
    sqlite3_recordset useData_;
    bool databaseReady_;
    bool boundByName_;
    bool boundByPos_;

    long long rowsAffectedBulk_; // number of rows affected by the last bulk operation

private:
    exec_fetch_result load_rowset(int totalRows);
    exec_fetch_result load_one();
    exec_fetch_result bind_and_execute(int number);
};

struct sqlite3_rowid_backend : details::rowid_backend
{
    sqlite3_rowid_backend(sqlite3_session_backend &session);

    ~sqlite3_rowid_backend();

    unsigned long value_;
};

struct sqlite3_blob_backend : details::blob_backend
{
    sqlite3_blob_backend(sqlite3_session_backend &session);

    ~sqlite3_blob_backend();

    virtual std::size_t get_len();
    virtual std::size_t read(std::size_t offset, char *buf,
                             std::size_t toRead);
    virtual std::size_t write(std::size_t offset, char const *buf,
                              std::size_t toWrite);
    virtual std::size_t append(char const *buf, std::size_t toWrite);
    virtual void trim(std::size_t newLen);

    sqlite3_session_backend &session_;

    std::size_t set_data(char const *buf, std::size_t toWrite);

private:
    char *buf_;
    size_t len_;
};

struct sqlite3_session_backend : details::session_backend
{
    sqlite3_session_backend(connection_parameters const & parameters);

    ~sqlite3_session_backend();

    virtual void begin();
    virtual void commit();
    virtual void rollback();

    virtual std::string get_backend_name() const { return "sqlite3"; }

    void clean_up();

    virtual sqlite3_statement_backend * make_statement_backend();
    virtual sqlite3_rowid_backend * make_rowid_backend();
    virtual sqlite3_blob_backend * make_blob_backend();

    sqlite_api::sqlite3 *conn_;
};

struct sqlite3_backend_factory : backend_factory
{
    sqlite3_backend_factory() {}
    virtual sqlite3_session_backend * make_session(
        connection_parameters const & parameters) const;
};

extern SOCI_SQLITE3_DECL sqlite3_backend_factory const sqlite3;

extern "C"
{

// for dynamic backend loading
SOCI_SQLITE3_DECL backend_factory const * factory_sqlite3();
SOCI_SQLITE3_DECL void register_factory_sqlite3();

} // extern "C"

} // namespace soci

#endif // SOCI_SQLITE3_H_INCLUDED
