//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_ODBC_H_INCLUDED
#define SOCI_ODBC_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_ODBC_SOURCE
#   define SOCI_ODBC_DECL __declspec(dllexport)
#  else
#   define SOCI_ODBC_DECL __declspec(dllimport)
#  endif // SOCI_ODBC_SOURCE
# endif // SOCI_DLL
#endif // _WIN32
//
// If SOCI_ODBC_DECL isn't defined yet define it now
#ifndef SOCI_ODBC_DECL
# define SOCI_ODBC_DECL
#endif

#include "soci/soci-platform.h"
#include <vector>
#include <soci/soci-backend.h>
#include <sstream>
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <windows.h>
#endif
#include <sqlext.h> // ODBC
#include <string.h> // strcpy()

#ifndef SQL_SS_LENGTH_UNLIMITED
#define SQL_SS_LENGTH_UNLIMITED 0
#endif

namespace soci
{

namespace details
{
    // TODO: Do we want to make it a part of public interface? --mloskot
    std::size_t const odbc_max_buffer_length = 100 * 1024 * 1024;

    // select max size from following MSDN article
    // https://msdn.microsoft.com/en-us/library/ms130896.aspx
    SQLLEN const ODBC_MAX_COL_SIZE = 8000;

    // This cast is only used to avoid compiler warnings when passing strings
    // to ODBC functions, the returned string may *not* be really modified.
    inline SQLCHAR* sqlchar_cast(std::string const& s)
    {
      return reinterpret_cast<SQLCHAR*>(const_cast<char*>(s.c_str()));
    }
}

// Option allowing to specify the "driver completion" parameter of
// SQLDriverConnect(). Its possible values are the same as the allowed values
// for this parameter in the official ODBC, i.e. one of SQL_DRIVER_XXX (in
// string form as all options are strings currently).
extern SOCI_ODBC_DECL char const * odbc_option_driver_complete;

struct odbc_statement_backend;

// Helper of into and use backends.
class odbc_standard_type_backend_base
{
protected:
    odbc_standard_type_backend_base(odbc_statement_backend &st)
        : statement_(st) {}

    // Check if we need to pass 64 bit integers as strings to the database as
    // some drivers don't support them directly.
    inline bool use_string_for_bigint() const;

    // If we do need to use strings for 64 bit integers, this constant defines
    // the maximal string length needed.
    enum
    {
        // This is the length of decimal representation of UINT64_MAX + 1.
        max_bigint_length = 21
    };

    odbc_statement_backend &statement_;
private:
    SOCI_NOT_COPYABLE(odbc_standard_type_backend_base)
};

struct odbc_standard_into_type_backend : details::standard_into_type_backend,
                                         private odbc_standard_type_backend_base
{
    odbc_standard_into_type_backend(odbc_statement_backend &st)
        : odbc_standard_type_backend_base(st), buf_(0)
    {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch,
        indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    char *buf_;        // generic buffer
    void *data_;
    details::exchange_type type_;
    int position_;
    SQLSMALLINT odbcType_;
    SQLLEN valueLen_;
private:
    SOCI_NOT_COPYABLE(odbc_standard_into_type_backend)
};

struct odbc_vector_into_type_backend : details::vector_into_type_backend,
                                       private odbc_standard_type_backend_base
{
    odbc_vector_into_type_backend(odbc_statement_backend &st)
        : odbc_standard_type_backend_base(st), indHolders_(NULL),
          data_(NULL), buf_(NULL) {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    // helper function for preparing indicators
    // (as part of the define_by_pos)
    void prepare_indicators(std::size_t size);


    SQLLEN *indHolders_;
    std::vector<SQLLEN> indHolderVec_;
    void *data_;
    char *buf_;              // generic buffer
    details::exchange_type type_;
    std::size_t colSize_;    // size of the string column (used for strings)
    SQLSMALLINT odbcType_;
};

struct odbc_standard_use_type_backend : details::standard_use_type_backend,
                                        private odbc_standard_type_backend_base
{
    odbc_standard_use_type_backend(odbc_statement_backend &st)
        : odbc_standard_type_backend_base(st),
          position_(-1), data_(0), buf_(0), indHolder_(0) {}

    void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    // Return the pointer to the buffer containing data to be used by ODBC.
    // This can be either data_ itself or buf_, that is allocated by this
    // function if necessary.
    //
    // Also fill in the size of the data and SQL and C types of it.
    void* prepare_for_bind(SQLLEN &size,
       SQLSMALLINT &sqlType, SQLSMALLINT &cType);

    int position_;
    void *data_;
    details::exchange_type type_;
    char *buf_;
    SQLLEN indHolder_;

private:
    // Copy string data to buf_ and set size, sqlType and cType to the values
    // appropriate for strings.
    void copy_from_string(std::string const& s,
                          SQLLEN& size,
                          SQLSMALLINT& sqlType,
                          SQLSMALLINT& cType);

};

struct odbc_vector_use_type_backend : details::vector_use_type_backend,
                                      private odbc_standard_type_backend_base
{
    odbc_vector_use_type_backend(odbc_statement_backend &st)
        : odbc_standard_type_backend_base(st), indHolders_(NULL),
          data_(NULL), buf_(NULL) {}

    // helper function for preparing indicators
    // (as part of the define_by_pos)
    void prepare_indicators(std::size_t size);

    // common part for bind_by_pos and bind_by_name
    void prepare_for_bind(void *&data, SQLUINTEGER &size, SQLSMALLINT &sqlType, SQLSMALLINT &cType);
    void bind_helper(int &position,
        void *data, details::exchange_type type);

    void bind_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;


    SQLLEN *indHolders_;
    std::vector<SQLLEN> indHolderVec_;
    void *data_;
    details::exchange_type type_;
    char *buf_;              // generic buffer
    std::size_t colSize_;    // size of the string column (used for strings)
    // used for strings only
    std::size_t maxSize_;
};

struct odbc_session_backend;
struct odbc_statement_backend : details::statement_backend
{
    odbc_statement_backend(odbc_session_backend &session);

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

    // helper for defining into vector<string>
    std::size_t column_size(int position);

    odbc_standard_into_type_backend * make_into_type_backend() SOCI_OVERRIDE;
    odbc_standard_use_type_backend * make_use_type_backend() SOCI_OVERRIDE;
    odbc_vector_into_type_backend * make_vector_into_type_backend() SOCI_OVERRIDE;
    odbc_vector_use_type_backend * make_vector_use_type_backend() SOCI_OVERRIDE;

    odbc_session_backend &session_;
    SQLHSTMT hstmt_;
    SQLULEN numRowsFetched_;
    bool hasVectorUseElements_;
    bool boundByName_;
    bool boundByPos_;

    long long rowsAffected_; // number of rows affected by the last operation

    std::string query_;
    std::vector<std::string> names_; // list of names for named binds

};

struct odbc_rowid_backend : details::rowid_backend
{
    odbc_rowid_backend(odbc_session_backend &session);

    ~odbc_rowid_backend() SOCI_OVERRIDE;
};

struct odbc_blob_backend : details::blob_backend
{
    odbc_blob_backend(odbc_session_backend &session);

    ~odbc_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;
    std::size_t read(std::size_t offset, char *buf,
        std::size_t toRead) SOCI_OVERRIDE;
    std::size_t write(std::size_t offset, char const *buf,
        std::size_t toWrite) SOCI_OVERRIDE;
    std::size_t append(char const *buf, std::size_t toWrite) SOCI_OVERRIDE;
    void trim(std::size_t newLen) SOCI_OVERRIDE;

    odbc_session_backend &session_;
};

struct odbc_session_backend : details::session_backend
{
    odbc_session_backend(connection_parameters const & parameters);

    ~odbc_session_backend() SOCI_OVERRIDE;

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    bool get_next_sequence_value(session & s,
        std::string const & sequence, long & value) SOCI_OVERRIDE;
    bool get_last_insert_id(session & s,
        std::string const & table, long & value) SOCI_OVERRIDE;

    std::string get_dummy_from_table() const SOCI_OVERRIDE;

    std::string get_backend_name() const SOCI_OVERRIDE { return "odbc"; }

    void configure_connection();
    void reset_transaction();

    void clean_up();

    odbc_statement_backend * make_statement_backend() SOCI_OVERRIDE;
    odbc_rowid_backend * make_rowid_backend() SOCI_OVERRIDE;
    odbc_blob_backend * make_blob_backend() SOCI_OVERRIDE;

    enum database_product
    {
      prod_uninitialized, // Never returned by get_database_product().
      prod_firebird,
      prod_mssql,
      prod_mysql,
      prod_oracle,
      prod_postgresql,
      prod_sqlite,
      prod_unknown = -1
    };

    // Determine the type of the database we're connected to.
    database_product get_database_product() const;

    // Return full ODBC connection string.
    std::string get_connection_string() const { return connection_string_; }

    SQLHENV henv_;
    SQLHDBC hdbc_;

    std::string connection_string_;

private:
    mutable database_product product_;
};

class SOCI_ODBC_DECL odbc_soci_error : public soci_error
{
    SQLCHAR message_[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLCHAR sqlstate_[SQL_SQLSTATE_SIZE + 1];
    SQLINTEGER sqlcode_;

public:
    odbc_soci_error(SQLSMALLINT htype,
                  SQLHANDLE hndl,
                  std::string const & msg)
        : soci_error(interpret_odbc_error(htype, hndl, msg))
    {
    }

    SQLCHAR const * odbc_error_code() const
    {
        return sqlstate_;
    }
    SQLINTEGER native_error_code() const
    {
        return sqlcode_;
    }
    SQLCHAR const * odbc_error_message() const
    {
        return message_;
    }
private:
    std::string interpret_odbc_error(SQLSMALLINT htype, SQLHANDLE hndl, std::string const& msg)
    {
        const char* socierror = NULL;

        SQLSMALLINT length, i = 1;
        switch ( SQLGetDiagRecA(htype, hndl, i, sqlstate_, &sqlcode_,
                               message_, SQL_MAX_MESSAGE_LENGTH + 1,
                               &length) )
        {
          case SQL_SUCCESS:
            // The error message was successfully retrieved.
            break;

          case SQL_INVALID_HANDLE:
            socierror = "[SOCI]: Invalid handle.";
            break;

          case SQL_ERROR:
            socierror = "[SOCI]: SQLGetDiagRec() error.";
            break;

          case SQL_SUCCESS_WITH_INFO:
            socierror = "[SOCI]: Error message too long.";
            break;

          case SQL_NO_DATA:
            socierror = "[SOCI]: No error.";
            break;

          default:
            socierror = "[SOCI]: Unexpected SQLGetDiagRec() return value.";
            break;
        }

        if (socierror)
        {
            // Use our own error message if we failed to retrieve the ODBC one.
            strcpy(reinterpret_cast<char*>(message_), socierror);

            // Use "General warning" SQLSTATE code.
            strcpy(reinterpret_cast<char*>(sqlstate_), "01000");

            sqlcode_ = 0;
        }

        std::ostringstream ss;
        ss << "Error " << msg << ": " << message_ << " (SQL state " << sqlstate_ << ")";

        return ss.str();
    }
};

inline bool is_odbc_error(SQLRETURN rc)
{
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO && rc != SQL_NO_DATA)
    {
        return true;
    }
    else
    {
        return false;
    }
}

inline bool odbc_standard_type_backend_base::use_string_for_bigint() const
{
    // Oracle ODBC driver doesn't support SQL_C_[SU]BIGINT data types
    // (see appendix G.1 of Oracle Database Administrator's reference at
    // http://docs.oracle.com/cd/B19306_01/server.102/b15658/app_odbc.htm),
    // so we need a special workaround for this case and we represent 64
    // bit integers as strings and rely on ODBC driver for transforming
    // them to SQL_NUMERIC.
    return statement_.session_.get_database_product()
            == odbc_session_backend::prod_oracle;
}

struct odbc_backend_factory : backend_factory
{
    odbc_backend_factory() {}
    odbc_session_backend * make_session(
        connection_parameters const & parameters) const SOCI_OVERRIDE;
};

extern SOCI_ODBC_DECL odbc_backend_factory const odbc;

extern "C"
{

// for dynamic backend loading
SOCI_ODBC_DECL backend_factory const * factory_odbc();
SOCI_ODBC_DECL void register_factory_odbc();

} // extern "C"

} // namespace soci

#endif // SOCI_EMPTY_H_INCLUDED
