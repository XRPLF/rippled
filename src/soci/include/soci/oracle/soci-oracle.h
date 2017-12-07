//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_ORACLE_H_INCLUDED
#define SOCI_ORACLE_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_ORACLE_SOURCE
#   define SOCI_ORACLE_DECL __declspec(dllexport)
#  else
#   define SOCI_ORACLE_DECL __declspec(dllimport)
#  endif // SOCI_ORACLE_SOURCE
# endif // SOCI_DLL
#endif // _WIN32
//
// If SOCI_ORACLE_DECL isn't defined yet define it now
#ifndef SOCI_ORACLE_DECL
# define SOCI_ORACLE_DECL
#endif

#include <soci/soci-backend.h>
#include <oci.h> // OCI
#include <sstream>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable:4512 4511)
#endif


namespace soci
{

class SOCI_ORACLE_DECL oracle_soci_error : public soci_error
{
public:
    oracle_soci_error(std::string const & msg, int errNum = 0);

    error_category get_error_category() const SOCI_OVERRIDE { return cat_; }

    int err_num_;
    error_category cat_;
};


struct oracle_statement_backend;
struct oracle_standard_into_type_backend : details::standard_into_type_backend
{
    oracle_standard_into_type_backend(oracle_statement_backend &st)
        : statement_(st), defnp_(NULL), indOCIHolder_(0),
          data_(NULL), buf_(NULL) {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void read_from_lob(OCILobLocator * lobp, std::string & value);

    void pre_exec(int num) SOCI_OVERRIDE;
    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch,
        indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    oracle_statement_backend &statement_;

    OCIDefine *defnp_;
    sb2 indOCIHolder_;
    void *data_;
    void *ociData_;
    char *buf_;        // generic buffer
    details::exchange_type type_;

    ub2 rCode_;
};

struct oracle_vector_into_type_backend : details::vector_into_type_backend
{
    oracle_vector_into_type_backend(oracle_statement_backend &st)
        : statement_(st), defnp_(NULL), indOCIHolders_(NULL),
        data_(NULL), buf_(NULL), user_ranges_(true) {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE
    {
        user_ranges_ = false;
        define_by_pos_bulk(position, data, type, 0, &end_var_);
    }

    void define_by_pos_bulk(
        int & position, void * data, details::exchange_type type,
        std::size_t begin, std::size_t * end) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE;
    std::size_t full_size();

    void clean_up() SOCI_OVERRIDE;

    // helper function for preparing indicators and sizes_ vectors
    // (as part of the define_by_pos)
    void prepare_indicators(std::size_t size);

    oracle_statement_backend &statement_;

    OCIDefine *defnp_;
    sb2 *indOCIHolders_;
    std::vector<sb2> indOCIHolderVec_;
    void *data_;
    char *buf_;              // generic buffer
    details::exchange_type type_;
    std::size_t begin_;
    std::size_t * end_;
    std::size_t end_var_;
    bool user_ranges_;
    std::size_t colSize_;    // size of the string column (used for strings)
    std::vector<ub2> sizes_; // sizes of data fetched (used for strings)

    std::vector<ub2> rCodes_;
};

struct oracle_standard_use_type_backend : details::standard_use_type_backend
{
    oracle_standard_use_type_backend(oracle_statement_backend &st)
        : statement_(st), bindp_(NULL), indOCIHolder_(0),
          data_(NULL), buf_(NULL) {}

    void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    // common part for bind_by_pos and bind_by_name
    void prepare_for_bind(void *&data, sb4 &size, ub2 &oracleType, bool readOnly);

    // common helper for pre_use for LOB-directed wrapped types
    void write_to_lob(OCILobLocator * lobp, const std::string & value);

    // common lazy initialization of the temporary LOB object
    void lazy_temp_lob_init();

    void pre_exec(int num) SOCI_OVERRIDE;
    void pre_use(indicator const *ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    oracle_statement_backend &statement_;

    OCIBind *bindp_;
    sb2 indOCIHolder_;
    void *data_;
    void *ociData_;
    bool readOnly_;
    char *buf_;        // generic buffer
    details::exchange_type type_;
};

struct oracle_vector_use_type_backend : details::vector_use_type_backend
{
    oracle_vector_use_type_backend(oracle_statement_backend &st)
        : statement_(st), bindp_(NULL), indOCIHolders_(NULL),
          data_(NULL), buf_(NULL) {}

    void bind_by_pos(int & position,
        void * data, details::exchange_type type) SOCI_OVERRIDE
    {
        bind_by_pos_bulk(position, data, type, 0, &end_var_);
    }

    void bind_by_pos_bulk(int & position,
        void * data, details::exchange_type type,
        std::size_t begin, std::size_t * end) SOCI_OVERRIDE;

    void bind_by_name(const std::string & name,
        void * data, details::exchange_type type) SOCI_OVERRIDE
    {
        bind_by_name_bulk(name, data, type, 0, &end_var_);
    }

    void bind_by_name_bulk(std::string const &name,
        void *data, details::exchange_type type,
        std::size_t begin, std::size_t * end) SOCI_OVERRIDE;

    // common part for bind_by_pos and bind_by_name
    void prepare_for_bind(void *&data, sb4 &size, ub2 &oracleType);

    // helper function for preparing indicators and sizes_ vectors
    // (as part of the bind_by_pos and bind_by_name)
    void prepare_indicators(std::size_t size);

    void pre_use(indicator const *ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE; // active size (might be lower than full vector size)
    std::size_t full_size();    // actual size of the user-provided vector

    void clean_up() SOCI_OVERRIDE;

    oracle_statement_backend &statement_;

    OCIBind *bindp_;
    std::vector<sb2> indOCIHolderVec_;
    sb2 *indOCIHolders_;
    void *data_;
    char *buf_;        // generic buffer
    details::exchange_type type_;
    std::size_t begin_;
    std::size_t * end_;
    std::size_t end_var_;

    // used for strings only
    std::vector<ub2> sizes_;
    std::size_t maxSize_;
};

struct oracle_session_backend;
struct oracle_statement_backend : details::statement_backend
{
    oracle_statement_backend(oracle_session_backend &session);

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

    oracle_standard_into_type_backend * make_into_type_backend() SOCI_OVERRIDE;
    oracle_standard_use_type_backend * make_use_type_backend() SOCI_OVERRIDE;
    oracle_vector_into_type_backend * make_vector_into_type_backend() SOCI_OVERRIDE;
    oracle_vector_use_type_backend * make_vector_use_type_backend() SOCI_OVERRIDE;

    oracle_session_backend &session_;

    OCIStmt *stmtp_;

    bool boundByName_;
    bool boundByPos_;
    bool noData_;
};

struct oracle_rowid_backend : details::rowid_backend
{
    oracle_rowid_backend(oracle_session_backend &session);

    ~oracle_rowid_backend() SOCI_OVERRIDE;

    OCIRowid *rowidp_;
};

struct oracle_blob_backend : details::blob_backend
{
    oracle_blob_backend(oracle_session_backend &session);

    ~oracle_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;

    std::size_t read(std::size_t offset, char *buf,
        std::size_t toRead) SOCI_OVERRIDE;

    std::size_t read_from_start(char * buf, std::size_t toRead,
        std::size_t offset) SOCI_OVERRIDE
    {
        return read(offset + 1, buf, toRead);
    }

    std::size_t write(std::size_t offset, char const *buf,
        std::size_t toWrite) SOCI_OVERRIDE;

    std::size_t write_from_start(const char * buf, std::size_t toWrite,
        std::size_t offset) SOCI_OVERRIDE
    {
        return write(offset + 1, buf, toWrite);
    }

    std::size_t append(char const *buf, std::size_t toWrite) SOCI_OVERRIDE;

    void trim(std::size_t newLen) SOCI_OVERRIDE;

    oracle_session_backend &session_;

    OCILobLocator *lobp_;
};

struct oracle_session_backend : details::session_backend
{
    oracle_session_backend(std::string const & serviceName,
        std::string const & userName,
        std::string const & password,
        int mode,
        bool decimals_as_strings = false,
        int charset = 0,
        int ncharset = 0);

    ~oracle_session_backend() SOCI_OVERRIDE;

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    std::string get_table_names_query() const SOCI_OVERRIDE
    {
        return "select table_name"
            " from user_tables";
    }

    std::string get_column_descriptions_query() const SOCI_OVERRIDE
    {
        return "select column_name,"
            " data_type,"
            " char_length as character_maximum_length,"
            " data_precision as numeric_precision,"
            " data_scale as numeric_scale,"
            " decode(nullable, 'Y', 'YES', 'N', 'NO') as is_nullable"
            " from user_tab_columns"
            " where table_name = :t";
    }

    std::string create_column_type(data_type dt,
        int precision, int scale) SOCI_OVERRIDE
    {
        //  Oracle-specific SQL syntax:

        std::string res;
        switch (dt)
        {
        case dt_string:
            {
                std::ostringstream oss;

                if (precision == 0)
                {
                    oss << "clob";
                }
                else
                {
                    oss << "varchar(" << precision << ")";
                }

                res += oss.str();
            }
            break;

        case dt_date:
            res += "timestamp";
            break;

        case dt_double:
            {
                std::ostringstream oss;
                if (precision == 0)
                {
                    oss << "number";
                }
                else
                {
                    oss << "number(" << precision << ", " << scale << ")";
                }

                res += oss.str();
            }
            break;

        case dt_integer:
            res += "integer";
            break;

        case dt_long_long:
            res += "number";
            break;

        case dt_unsigned_long_long:
            res += "number";
            break;

        case dt_blob:
            res += "blob";
            break;

        case dt_xml:
            res += "xmltype";
            break;

        default:
            throw soci_error("this data_type is not supported in create_column");
        }

        return res;
    }
    std::string add_column(const std::string & tableName,
        const std::string & columnName, data_type dt,
        int precision, int scale) SOCI_OVERRIDE
    {
        return "alter table " + tableName + " add " +
            columnName + " " + create_column_type(dt, precision, scale);
    }
    std::string alter_column(const std::string & tableName,
        const std::string & columnName, data_type dt,
        int precision, int scale) SOCI_OVERRIDE
    {
        return "alter table " + tableName + " modify " +
            columnName + " " + create_column_type(dt, precision, scale);
    }
    std::string empty_blob() SOCI_OVERRIDE
    {
        return "empty_blob()";
    }
    std::string nvl() SOCI_OVERRIDE
    {
        return "nvl";
    }

    std::string get_dummy_from_table() const SOCI_OVERRIDE { return "dual"; }

    std::string get_backend_name() const SOCI_OVERRIDE { return "oracle"; }

    void clean_up();

    oracle_statement_backend * make_statement_backend() SOCI_OVERRIDE;
    oracle_rowid_backend * make_rowid_backend() SOCI_OVERRIDE;
    oracle_blob_backend * make_blob_backend() SOCI_OVERRIDE;

    bool get_option_decimals_as_strings() { return decimals_as_strings_; }

    // Return either SQLT_FLT or SQLT_BDOUBLE as the type to use when binding
    // values of C type "double" (the latter is preferable but might not be
    // always available).
    ub2 get_double_sql_type() const;

    OCIEnv *envhp_;
    OCIServer *srvhp_;
    OCIError *errhp_;
    OCISvcCtx *svchp_;
    OCISession *usrhp_;
    bool decimals_as_strings_;
};

struct oracle_backend_factory : backend_factory
{
	  oracle_backend_factory() {}
    oracle_session_backend * make_session(
        connection_parameters const & parameters) const SOCI_OVERRIDE;
};

extern SOCI_ORACLE_DECL oracle_backend_factory const oracle;

extern "C"
{

// for dynamic backend loading
SOCI_ORACLE_DECL backend_factory const * factory_oracle();
SOCI_ORACLE_DECL void register_factory_oracle();

} // extern "C"

} // namespace soci

#endif
