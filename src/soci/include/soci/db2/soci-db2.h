//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_DB2_H_INCLUDED
#define SOCI_DB2_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_DB2_SOURCE
#   define SOCI_DB2_DECL __declspec(dllexport)
#  else
#   define SOCI_DB2_DECL __declspec(dllimport)
#  endif // SOCI_DB2_SOURCE
# endif // SOCI_DLL
#endif // _WIN32
//
// If SOCI_DB2_DECL isn't defined yet define it now
#ifndef SOCI_DB2_DECL
# define SOCI_DB2_DECL
#endif

#include <soci/soci-backend.h>

#include <cstddef>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>

#include <sqlcli1.h>

namespace soci
{
    namespace details { namespace db2
    {
        enum binding_method
        {
            BOUND_BY_NONE,
            BOUND_BY_NAME,
            BOUND_BY_POSITION
        };

        inline SQLPOINTER int_as_ptr(int n)
        {
            union
            {
                SQLPOINTER p;
                int n;
            } u;
            u.n = n;
            return u.p;
        }
    }}

    static const std::size_t maxBuffer =  1024 * 1024 * 1024; //CLI limit is about 3 GB, but 1GB should be enough

class SOCI_DB2_DECL db2_soci_error : public soci_error {
public:
    db2_soci_error(std::string const & msg, SQLRETURN rc) : soci_error(msg),errorCode(rc) {};
    ~db2_soci_error() throw() SOCI_OVERRIDE { };

    //We have to extract error information before exception throwing, cause CLI handles could be broken at the construction time
    static const std::string sqlState(std::string const & msg,const SQLSMALLINT htype,const SQLHANDLE hndl);

    SQLRETURN errorCode;
};

// Option allowing to specify the "driver completion" parameter of
// SQLDriverConnect(). Its possible values are the same as the allowed values
// for this parameter in the official DB2 CLI, i.e. one of SQL_DRIVER_XXX
// (in string form as all options are strings currently).
extern SOCI_DB2_DECL char const * db2_option_driver_complete;

struct db2_statement_backend;

struct SOCI_DB2_DECL db2_standard_into_type_backend : details::standard_into_type_backend
{
    db2_standard_into_type_backend(db2_statement_backend &st)
        : statement_(st),buf(NULL)
    {}

    void define_by_pos(int& position, void* data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch, indicator* ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    db2_statement_backend& statement_;

    char* buf;
    void *data;
    details::exchange_type type;
    int position;
    SQLSMALLINT cType;
    SQLLEN valueLen;
};

struct SOCI_DB2_DECL db2_vector_into_type_backend : details::vector_into_type_backend
{
    db2_vector_into_type_backend(db2_statement_backend &st)
        : statement_(st),buf(NULL)
    {}

    void define_by_pos(int& position, void* data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator* ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    db2_statement_backend& statement_;

    void prepare_indicators(std::size_t size);

    SQLLEN *indptr;
    std::vector<SQLLEN> indVec;
    void *data;
    char *buf;
    int position_;
    details::exchange_type type;
    SQLSMALLINT cType;
    std::size_t colSize;
};

struct SOCI_DB2_DECL db2_standard_use_type_backend : details::standard_use_type_backend
{
    db2_standard_use_type_backend(db2_statement_backend &st)
        : statement_(st),buf(NULL),ind(0)
    {}

    void bind_by_pos(int& position, void* data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const& name, void* data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    void pre_use(indicator const* ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator* ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    db2_statement_backend& statement_;

    void *prepare_for_bind(void *data, SQLLEN &size, SQLSMALLINT &sqlType, SQLSMALLINT &cType);

    void *data;
    details::exchange_type type;
    int position;
    std::string name;
    char* buf;
    SQLLEN ind;
};

struct SOCI_DB2_DECL db2_vector_use_type_backend : details::vector_use_type_backend
{
    db2_vector_use_type_backend(db2_statement_backend &st)
        : statement_(st),buf(NULL) {}

    void bind_by_pos(int& position, void* data, details::exchange_type type) SOCI_OVERRIDE;
    void bind_by_name(std::string const& name, void* data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_use(indicator const* ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    db2_statement_backend& statement_;

    void prepare_indicators(std::size_t size);
    void prepare_for_bind(void *&data, SQLUINTEGER &size,SQLSMALLINT &sqlType, SQLSMALLINT &cType);
    void bind_helper(int &position, void *data, details::exchange_type type);

    SQLLEN *indptr;
    std::vector<SQLLEN> indVec;
    void *data;
    char *buf;
    details::exchange_type type;
    std::size_t colSize;
};

struct db2_session_backend;
struct SOCI_DB2_DECL db2_statement_backend : details::statement_backend
{
    db2_statement_backend(db2_session_backend &session);

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
    size_t column_size(int col);

    db2_standard_into_type_backend* make_into_type_backend() SOCI_OVERRIDE;
    db2_standard_use_type_backend* make_use_type_backend() SOCI_OVERRIDE;
    db2_vector_into_type_backend* make_vector_into_type_backend() SOCI_OVERRIDE;
    db2_vector_use_type_backend* make_vector_use_type_backend() SOCI_OVERRIDE;

    db2_session_backend& session_;

    SQLHANDLE hStmt;
    std::string query_;
    std::vector<std::string> names_;
    bool hasVectorUseElements;
    SQLUINTEGER numRowsFetched;
    details::db2::binding_method use_binding_method_;
};

struct db2_rowid_backend : details::rowid_backend
{
    db2_rowid_backend(db2_session_backend &session);

    ~db2_rowid_backend() SOCI_OVERRIDE;
};

struct db2_blob_backend : details::blob_backend
{
    db2_blob_backend(db2_session_backend& session);

    ~db2_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;
    std::size_t read(std::size_t offset, char* buf, std::size_t toRead) SOCI_OVERRIDE;
    std::size_t write(std::size_t offset, char const* buf, std::size_t toWrite) SOCI_OVERRIDE;
    std::size_t append(char const* buf, std::size_t toWrite) SOCI_OVERRIDE;
    void trim(std::size_t newLen) SOCI_OVERRIDE;

    db2_session_backend& session_;
};

struct db2_session_backend : details::session_backend
{
    db2_session_backend(connection_parameters const& parameters);

    ~db2_session_backend() SOCI_OVERRIDE;

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    std::string get_dummy_from_table() const SOCI_OVERRIDE { return "sysibm.sysdummy1"; }

    std::string get_backend_name() const SOCI_OVERRIDE { return "DB2"; }

    void clean_up();

    db2_statement_backend* make_statement_backend() SOCI_OVERRIDE;
    db2_rowid_backend* make_rowid_backend() SOCI_OVERRIDE;
    db2_blob_backend* make_blob_backend() SOCI_OVERRIDE;

    void parseConnectString(std::string const &);
    void parseKeyVal(std::string const &);

    std::string connection_string_;
    bool autocommit;
    bool in_transaction;

    SQLHANDLE hEnv; /* Environment handle */
    SQLHANDLE hDbc; /* Connection handle */
};

struct SOCI_DB2_DECL db2_backend_factory : backend_factory
{
    db2_backend_factory() {}
    db2_session_backend* make_session(
        connection_parameters const & parameters) const SOCI_OVERRIDE;
};

extern SOCI_DB2_DECL db2_backend_factory const db2;

extern "C"
{

// for dynamic backend loading
SOCI_DB2_DECL backend_factory const* factory_db2();
SOCI_DB2_DECL void register_factory_db2();

} // extern "C"

} // namespace soci

#endif // SOCI_DB2_H_INCLUDED
