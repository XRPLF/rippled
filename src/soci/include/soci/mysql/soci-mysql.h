//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_MYSQL_H_INCLUDED
#define SOCI_MYSQL_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_MYSQL_SOURCE
#   define SOCI_MYSQL_DECL __declspec(dllexport)
#  else
#   define SOCI_MYSQL_DECL __declspec(dllimport)
#  endif // SOCI_DLL
# endif // SOCI_MYSQL_SOURCE
#endif // _WIN32
//
// If SOCI_MYSQL_DECL isn't defined yet define it now
#ifndef SOCI_MYSQL_DECL
# define SOCI_MYSQL_DECL
#endif

#include <soci/soci-backend.h>
#ifdef _WIN32
#include <winsock.h> // SOCKET
#endif // _WIN32
#include <mysql.h> // MySQL Client
#include <vector>


namespace soci
{

class SOCI_MYSQL_DECL mysql_soci_error : public soci_error
{
public:
    mysql_soci_error(std::string const & msg, int errNum)
        : soci_error(msg), err_num_(errNum) {}

    unsigned int err_num_;
};

struct mysql_statement_backend;
struct mysql_standard_into_type_backend : details::standard_into_type_backend
{
    mysql_standard_into_type_backend(mysql_statement_backend &st)
        : statement_(st) {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch,
        indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    mysql_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
};

struct mysql_vector_into_type_backend : details::vector_into_type_backend
{
    mysql_vector_into_type_backend(mysql_statement_backend &st)
        : statement_(st) {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    mysql_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
};

struct mysql_standard_use_type_backend : details::standard_use_type_backend
{
    mysql_standard_use_type_backend(mysql_statement_backend &st)
        : statement_(st), position_(0), buf_(NULL) {}

    void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    mysql_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
    std::string name_;
    char *buf_;
};

struct mysql_vector_use_type_backend : details::vector_use_type_backend
{
    mysql_vector_use_type_backend(mysql_statement_backend &st)
        : statement_(st), position_(0) {}

    void bind_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    mysql_statement_backend &statement_;

    void *data_;
    details::exchange_type type_;
    int position_;
    std::string name_;
    std::vector<char *> buffers_;
};

struct mysql_session_backend;
struct mysql_statement_backend : details::statement_backend
{
    mysql_statement_backend(mysql_session_backend &session);

    void alloc() SOCI_OVERRIDE;
    void clean_up() SOCI_OVERRIDE;
    void prepare(std::string const &query,
        details::statement_type eType) SOCI_OVERRIDE;

    exec_fetch_result execute(int number) SOCI_OVERRIDE;
    exec_fetch_result fetch(int number) SOCI_OVERRIDE;

    long long get_affected_rows() SOCI_OVERRIDE;
    int get_number_of_rows() SOCI_OVERRIDE;
    std::string get_parameter_name(int index) const SOCI_OVERRIDE;

    std::string rewrite_for_procedure_call(std::string const &query) SOCI_OVERRIDE;

    int prepare_for_describe() SOCI_OVERRIDE;
    void describe_column(int colNum, data_type &dtype,
        std::string &columnName) SOCI_OVERRIDE;

    mysql_standard_into_type_backend * make_into_type_backend() SOCI_OVERRIDE;
    mysql_standard_use_type_backend * make_use_type_backend() SOCI_OVERRIDE;
    mysql_vector_into_type_backend * make_vector_into_type_backend() SOCI_OVERRIDE;
    mysql_vector_use_type_backend * make_vector_use_type_backend() SOCI_OVERRIDE;

    mysql_session_backend &session_;

    MYSQL_RES *result_;

    // The query is split into chunks, separated by the named parameters;
    // e.g. for "SELECT id FROM ttt WHERE name = :foo AND gender = :bar"
    // we will have query chunks "SELECT id FROM ttt WHERE name = ",
    // "AND gender = " and names "foo", "bar".
    std::vector<std::string> queryChunks_;
    std::vector<std::string> names_; // list of names for named binds

    long long rowsAffectedBulk_; // number of rows affected by the last bulk operation

    int numberOfRows_;  // number of rows retrieved from the server
    int currentRow_;    // "current" row number to consume in postFetch
    int rowsToConsume_; // number of rows to be consumed in postFetch

    bool justDescribed_; // to optimize row description with immediately
                         // following actual statement execution

    // Prefetch the row offsets in order to use mysql_row_seek() for
    // random access to rows, since mysql_data_seek() is expensive.
    std::vector<MYSQL_ROW_OFFSET> resultRowOffsets_;

    bool hasIntoElements_;
    bool hasVectorIntoElements_;
    bool hasUseElements_;
    bool hasVectorUseElements_;

    // the following maps are used for finding data buffers according to
    // use elements specified by the user

    typedef std::map<int, char **> UseByPosBuffersMap;
    UseByPosBuffersMap useByPosBuffers_;

    typedef std::map<std::string, char **> UseByNameBuffersMap;
    UseByNameBuffersMap useByNameBuffers_;
};

struct mysql_rowid_backend : details::rowid_backend
{
    mysql_rowid_backend(mysql_session_backend &session);

    ~mysql_rowid_backend() SOCI_OVERRIDE;
};

struct mysql_blob_backend : details::blob_backend
{
    mysql_blob_backend(mysql_session_backend &session);

    ~mysql_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;
    std::size_t read(std::size_t offset, char *buf,
        std::size_t toRead) SOCI_OVERRIDE;
    std::size_t write(std::size_t offset, char const *buf,
        std::size_t toWrite) SOCI_OVERRIDE;
    std::size_t append(char const *buf, std::size_t toWrite) SOCI_OVERRIDE;
    void trim(std::size_t newLen) SOCI_OVERRIDE;

    mysql_session_backend &session_;
};

struct mysql_session_backend : details::session_backend
{
    mysql_session_backend(connection_parameters const & parameters);

    ~mysql_session_backend() SOCI_OVERRIDE;

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    bool get_last_insert_id(session&, std::string const&, long&) SOCI_OVERRIDE;

    // Note that MySQL supports both "SELECT 2+2" and "SELECT 2+2 FROM DUAL"
    // syntaxes, but there doesn't seem to be any reason to use the longer one.
    std::string get_dummy_from_table() const SOCI_OVERRIDE { return std::string(); }

    std::string get_backend_name() const SOCI_OVERRIDE { return "mysql"; }

    void clean_up();

    mysql_statement_backend * make_statement_backend() SOCI_OVERRIDE;
    mysql_rowid_backend * make_rowid_backend() SOCI_OVERRIDE;
    mysql_blob_backend * make_blob_backend() SOCI_OVERRIDE;

    MYSQL *conn_;
};


struct mysql_backend_factory : backend_factory
{
    mysql_backend_factory() {}
    mysql_session_backend * make_session(
        connection_parameters const & parameters) const SOCI_OVERRIDE;
};

extern SOCI_MYSQL_DECL mysql_backend_factory const mysql;

extern "C"
{

// for dynamic backend loading
SOCI_MYSQL_DECL backend_factory const * factory_mysql();
SOCI_MYSQL_DECL void register_factory_mysql();

} // extern "C"

} // namespace soci

#endif // SOCI_MYSQL_H_INCLUDED
