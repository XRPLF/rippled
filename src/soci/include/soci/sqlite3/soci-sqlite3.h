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
#include <soci/soci-backend.h>

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

class SOCI_SQLITE3_DECL sqlite3_soci_error : public soci_error
{
public:
    sqlite3_soci_error(std::string const & msg, int result);

    int result() const;

private:
    int result_;
};

struct sqlite3_statement_backend;
struct sqlite3_standard_into_type_backend : details::standard_into_type_backend
{
    sqlite3_standard_into_type_backend(sqlite3_statement_backend &st)
        : statement_(st), data_(0), type_(), position_(0)
    {
    }

    void define_by_pos(int &position,
                             void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch,
                           indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    sqlite3_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
};

struct sqlite3_vector_into_type_backend : details::vector_into_type_backend
{
    sqlite3_vector_into_type_backend(sqlite3_statement_backend &st)
        : statement_(st), data_(0), type_(), position_(0)
    {
    }

    void define_by_pos(int& position, void* data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator* ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    sqlite3_statement_backend& statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
};

struct sqlite3_standard_use_type_backend : details::standard_use_type_backend
{
    sqlite3_standard_use_type_backend(sqlite3_statement_backend &st);

    void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    sqlite3_statement_backend &statement_;

    void *data_;                    // pointer to used data: soci::use(myvariable) --> data_ = &myvariable
    details::exchange_type type_;   // type of data_
    int position_;                  // binding position
    std::string name_;              // binding name
};

struct sqlite3_vector_use_type_backend : details::vector_use_type_backend
{
    sqlite3_vector_use_type_backend(sqlite3_statement_backend &st)
        : statement_(st), data_(0), type_(), position_(0)
    {
    }

    void bind_by_pos(int &position,
                           void *data, details::exchange_type type) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
                            void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    sqlite3_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
    std::string name_;
};

struct sqlite3_column_buffer
{
    std::size_t size_;
    union
    {
        const char *constData_;
        char *data_;
    };
};

struct sqlite3_column
{
    bool isNull_;
    data_type type_;

    union
    {
        sqlite3_column_buffer buffer_;
        int int32_;
        sqlite_api::sqlite3_int64 int64_;
        double double_;
    };
};

typedef std::vector<sqlite3_column> sqlite3_row;
typedef std::vector<sqlite3_row> sqlite3_recordset;


struct sqlite3_column_info
{
    data_type type_;
    std::string name_;
};
typedef std::vector<sqlite3_column_info> sqlite3_column_info_list;

struct sqlite3_session_backend;
struct sqlite3_statement_backend : details::statement_backend
{
    sqlite3_statement_backend(sqlite3_session_backend &session);

    void alloc() SOCI_OVERRIDE;
    void clean_up() SOCI_OVERRIDE;
    void prepare(std::string const &query,
        details::statement_type eType) SOCI_OVERRIDE;
    void reset_if_needed();
    void reset();

    exec_fetch_result execute(int number) SOCI_OVERRIDE;
    exec_fetch_result fetch(int number) SOCI_OVERRIDE;

    long long get_affected_rows() SOCI_OVERRIDE;
    int get_number_of_rows() SOCI_OVERRIDE;
    std::string get_parameter_name(int index) const SOCI_OVERRIDE;

    std::string rewrite_for_procedure_call(std::string const &query) SOCI_OVERRIDE;

    int prepare_for_describe() SOCI_OVERRIDE;
    void describe_column(int colNum, data_type &dtype,
                                std::string &columnName) SOCI_OVERRIDE;

    sqlite3_standard_into_type_backend * make_into_type_backend() SOCI_OVERRIDE;
    sqlite3_standard_use_type_backend * make_use_type_backend() SOCI_OVERRIDE;
    sqlite3_vector_into_type_backend * make_vector_into_type_backend() SOCI_OVERRIDE;
    sqlite3_vector_use_type_backend * make_vector_use_type_backend() SOCI_OVERRIDE;

    sqlite3_session_backend &session_;
    sqlite_api::sqlite3_stmt *stmt_;
    sqlite3_recordset dataCache_;
    sqlite3_recordset useData_;
    bool databaseReady_;
    bool boundByName_;
    bool boundByPos_;
    sqlite3_column_info_list columns_;


    long long rowsAffectedBulk_; // number of rows affected by the last bulk operation

private:
    exec_fetch_result load_rowset(int totalRows);
    exec_fetch_result load_one();
    exec_fetch_result bind_and_execute(int number);
};

struct sqlite3_rowid_backend : details::rowid_backend
{
    sqlite3_rowid_backend(sqlite3_session_backend &session);

    ~sqlite3_rowid_backend() SOCI_OVERRIDE;

    unsigned long value_;
};

struct sqlite3_blob_backend : details::blob_backend
{
    sqlite3_blob_backend(sqlite3_session_backend &session);

    ~sqlite3_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;
    std::size_t read(std::size_t offset, char *buf,
                             std::size_t toRead) SOCI_OVERRIDE;
    std::size_t write(std::size_t offset, char const *buf,
                              std::size_t toWrite) SOCI_OVERRIDE;
    std::size_t append(char const *buf, std::size_t toWrite) SOCI_OVERRIDE;
    void trim(std::size_t newLen) SOCI_OVERRIDE;

    sqlite3_session_backend &session_;

    std::size_t set_data(char const *buf, std::size_t toWrite);
    const char *get_buffer() const { return buf_; }

private:
    char *buf_;
    size_t len_;
};

struct sqlite3_session_backend : details::session_backend
{
    sqlite3_session_backend(connection_parameters const & parameters);

    ~sqlite3_session_backend() SOCI_OVERRIDE;

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    bool get_last_insert_id(session&, std::string const&, long&) SOCI_OVERRIDE;

    std::string empty_blob() SOCI_OVERRIDE
    {
        return "x\'\'";
    }

    std::string get_dummy_from_table() const SOCI_OVERRIDE { return std::string(); }

    std::string get_backend_name() const SOCI_OVERRIDE { return "sqlite3"; }

    void clean_up();

    sqlite3_statement_backend * make_statement_backend() SOCI_OVERRIDE;
    sqlite3_rowid_backend * make_rowid_backend() SOCI_OVERRIDE;
    sqlite3_blob_backend * make_blob_backend() SOCI_OVERRIDE;
    std::string get_table_names_query() const SOCI_OVERRIDE
    {
        return "select name as \"TABLE_NAME\""
                " from sqlite_master where type = 'table'";
    }
    std::string create_column_type(data_type dt,
                                           int , int ) SOCI_OVERRIDE
    {
        switch (dt)
        {
            case dt_xml:
            case dt_string:
                return "text";
            case dt_double:
                return "real";
            case dt_date:
            case dt_integer:
            case dt_long_long:
            case dt_unsigned_long_long:
                return "integer";
            case dt_blob:
                return "blob";
            default:
                throw soci_error("this data_type is not supported in create_column");
        }

    }
    sqlite_api::sqlite3 *conn_;
};

struct sqlite3_backend_factory : backend_factory
{
    sqlite3_backend_factory() {}
    sqlite3_session_backend * make_session(
        connection_parameters const & parameters) const SOCI_OVERRIDE;
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
