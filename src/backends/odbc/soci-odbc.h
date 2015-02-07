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

#include <vector>
#include <soci-backend.h>
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <soci-platform.h>
#include <windows.h>
#endif
#include <sqlext.h> // ODBC
#include <string.h> // strcpy()

namespace soci
{

    // TODO: Do we want to make it a part of public interface? --mloskot
namespace details
{
    std::size_t const odbc_max_buffer_length = 100 * 1024 * 1024;
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
};

struct odbc_standard_into_type_backend : details::standard_into_type_backend,
                                         private odbc_standard_type_backend_base
{
    odbc_standard_into_type_backend(odbc_statement_backend &st)
        : odbc_standard_type_backend_base(st), buf_(0)
    {}

    virtual void define_by_pos(int &position,
        void *data, details::exchange_type type);

    virtual void pre_fetch();
    virtual void post_fetch(bool gotData, bool calledFromFetch,
        indicator *ind);

    virtual void clean_up();

    char *buf_;        // generic buffer
    void *data_;
    details::exchange_type type_;
    int position_;
    SQLSMALLINT odbcType_;
    SQLLEN valueLen_;
};

struct odbc_vector_into_type_backend : details::vector_into_type_backend,
                                       private odbc_standard_type_backend_base
{
    odbc_vector_into_type_backend(odbc_statement_backend &st)
        : odbc_standard_type_backend_base(st), indHolders_(NULL),
          data_(NULL), buf_(NULL) {}

    virtual void define_by_pos(int &position,
        void *data, details::exchange_type type);

    virtual void pre_fetch();
    virtual void post_fetch(bool gotData, indicator *ind);

    virtual void resize(std::size_t sz);
    virtual std::size_t size();

    virtual void clean_up();

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

    virtual void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly);
    virtual void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly);

    virtual void pre_use(indicator const *ind);
    virtual void post_use(bool gotData, indicator *ind);

    virtual void clean_up();

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

    virtual void bind_by_pos(int &position,
        void *data, details::exchange_type type);
    virtual void bind_by_name(std::string const &name,
        void *data, details::exchange_type type);

    virtual void pre_use(indicator const *ind);

    virtual std::size_t size();

    virtual void clean_up();


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

    virtual void alloc();
    virtual void clean_up();
    virtual void prepare(std::string const &query,
        details::statement_type eType);

    virtual exec_fetch_result execute(int number);
    virtual exec_fetch_result fetch(int number);

    virtual long long get_affected_rows();
    virtual int get_number_of_rows();

    virtual std::string rewrite_for_procedure_call(std::string const &query);

    virtual int prepare_for_describe();
    virtual void describe_column(int colNum, data_type &dtype,
        std::string &columnName);

    // helper for defining into vector<string>
    std::size_t column_size(int position);

    virtual odbc_standard_into_type_backend * make_into_type_backend();
    virtual odbc_standard_use_type_backend * make_use_type_backend();
    virtual odbc_vector_into_type_backend * make_vector_into_type_backend();
    virtual odbc_vector_use_type_backend * make_vector_use_type_backend();

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

    ~odbc_rowid_backend();
};

struct odbc_blob_backend : details::blob_backend
{
    odbc_blob_backend(odbc_session_backend &session);

    ~odbc_blob_backend();

    virtual std::size_t get_len();
    virtual std::size_t read(std::size_t offset, char *buf,
        std::size_t toRead);
    virtual std::size_t write(std::size_t offset, char const *buf,
        std::size_t toWrite);
    virtual std::size_t append(char const *buf, std::size_t toWrite);
    virtual void trim(std::size_t newLen);

    odbc_session_backend &session_;
};

struct odbc_session_backend : details::session_backend
{
    odbc_session_backend(connection_parameters const & parameters);

    ~odbc_session_backend();

    virtual void begin();
    virtual void commit();
    virtual void rollback();

    virtual bool get_next_sequence_value(session & s,
        std::string const & sequence, long & value);
    virtual bool get_last_insert_id(session & s,
        std::string const & table, long & value);

    virtual std::string get_backend_name() const { return "odbc"; }

    void reset_transaction();

    void clean_up();

    virtual odbc_statement_backend * make_statement_backend();
    virtual odbc_rowid_backend * make_rowid_backend();
    virtual odbc_blob_backend * make_blob_backend();

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
    database_product get_database_product();

    // Return full ODBC connection string.
    std::string get_connection_string() const { return connection_string_; }

    SQLHENV henv_;
    SQLHDBC hdbc_;

    std::string connection_string_;
    database_product product_;
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
        : soci_error(msg)
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
    }

    SQLCHAR const * odbc_error_code() const
    {
        return reinterpret_cast<SQLCHAR const *>(sqlstate_);
    }
    SQLINTEGER native_error_code() const
    {
        return sqlcode_;
    }
    SQLCHAR const * odbc_error_message() const
    {
        return reinterpret_cast<SQLCHAR const *>(message_);
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
    virtual odbc_session_backend * make_session(
        connection_parameters const & parameters) const;
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
