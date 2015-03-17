//
// Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
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

#include <soci-backend.h>
#include <oci.h> // OCI
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

    int err_num_;
};


struct oracle_statement_backend;
struct oracle_standard_into_type_backend : details::standard_into_type_backend
{
    oracle_standard_into_type_backend(oracle_statement_backend &st)
        : statement_(st), defnp_(NULL), indOCIHolder_(0),
          data_(NULL), buf_(NULL) {}

    virtual void define_by_pos(int &position,
        void *data, details::exchange_type type);

    virtual void pre_fetch();
    virtual void post_fetch(bool gotData, bool calledFromFetch,
        indicator *ind);

    virtual void clean_up();

    oracle_statement_backend &statement_;

    OCIDefine *defnp_;
    sb2 indOCIHolder_;
    void *data_;
    char *buf_;        // generic buffer
    details::exchange_type type_;

    ub2 rCode_;
};

struct oracle_vector_into_type_backend : details::vector_into_type_backend
{
    oracle_vector_into_type_backend(oracle_statement_backend &st)
        : statement_(st), defnp_(NULL), indOCIHolders_(NULL),
          data_(NULL), buf_(NULL) {}

    virtual void define_by_pos(int &position,
        void *data, details::exchange_type type);

    virtual void pre_fetch();
    virtual void post_fetch(bool gotData, indicator *ind);

    virtual void resize(std::size_t sz);
    virtual std::size_t size();

    virtual void clean_up();

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
    std::size_t colSize_;    // size of the string column (used for strings)
    std::vector<ub2> sizes_; // sizes of data fetched (used for strings)

    std::vector<ub2> rCodes_;
};

struct oracle_standard_use_type_backend : details::standard_use_type_backend
{
    oracle_standard_use_type_backend(oracle_statement_backend &st)
        : statement_(st), bindp_(NULL), indOCIHolder_(0),
          data_(NULL), buf_(NULL) {}

    virtual void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly);
    virtual void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly);

    // common part for bind_by_pos and bind_by_name
    void prepare_for_bind(void *&data, sb4 &size, ub2 &oracleType, bool readOnly);

    virtual void pre_use(indicator const *ind);
    virtual void post_use(bool gotData, indicator *ind);

    virtual void clean_up();

    oracle_statement_backend &statement_;

    OCIBind *bindp_;
    sb2 indOCIHolder_;
    void *data_;
    bool readOnly_;
    char *buf_;        // generic buffer
    details::exchange_type type_;
};

struct oracle_vector_use_type_backend : details::vector_use_type_backend
{
    oracle_vector_use_type_backend(oracle_statement_backend &st)
        : statement_(st), bindp_(NULL), indOCIHolders_(NULL),
          data_(NULL), buf_(NULL) {}

    virtual void bind_by_pos(int &position,
        void *data, details::exchange_type type);
    virtual void bind_by_name(std::string const &name,
        void *data, details::exchange_type type);

    // common part for bind_by_pos and bind_by_name
    void prepare_for_bind(void *&data, sb4 &size, ub2 &oracleType);

    // helper function for preparing indicators and sizes_ vectors
    // (as part of the bind_by_pos and bind_by_name)
    void prepare_indicators(std::size_t size);

    virtual void pre_use(indicator const *ind);

    virtual std::size_t size();

    virtual void clean_up();

    oracle_statement_backend &statement_;

    OCIBind *bindp_;
    std::vector<sb2> indOCIHolderVec_;
    sb2 *indOCIHolders_;
    void *data_;
    char *buf_;        // generic buffer
    details::exchange_type type_;

    // used for strings only
    std::vector<ub2> sizes_;
    std::size_t maxSize_;
};

struct oracle_session_backend;
struct oracle_statement_backend : details::statement_backend
{
    oracle_statement_backend(oracle_session_backend &session);

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

    virtual oracle_standard_into_type_backend * make_into_type_backend();
    virtual oracle_standard_use_type_backend * make_use_type_backend();
    virtual oracle_vector_into_type_backend * make_vector_into_type_backend();
    virtual oracle_vector_use_type_backend * make_vector_use_type_backend();

    oracle_session_backend &session_;

    OCIStmt *stmtp_;

    bool boundByName_;
    bool boundByPos_;
    bool noData_;
};

struct oracle_rowid_backend : details::rowid_backend
{
    oracle_rowid_backend(oracle_session_backend &session);

    ~oracle_rowid_backend();

    OCIRowid *rowidp_;
};

struct oracle_blob_backend : details::blob_backend
{
    oracle_blob_backend(oracle_session_backend &session);

    ~oracle_blob_backend();

    virtual std::size_t get_len();
    virtual std::size_t read(std::size_t offset, char *buf,
        std::size_t toRead);
    virtual std::size_t write(std::size_t offset, char const *buf,
        std::size_t toWrite);
    virtual std::size_t append(char const *buf, std::size_t toWrite);
    virtual void trim(std::size_t newLen);

    oracle_session_backend &session_;

    OCILobLocator *lobp_;
};

struct oracle_session_backend : details::session_backend
{
    oracle_session_backend(std::string const & serviceName,
        std::string const & userName,
        std::string const & password,
        int mode,
        bool decimals_as_strings = false);

    ~oracle_session_backend();

    virtual void begin();
    virtual void commit();
    virtual void rollback();

    virtual std::string get_backend_name() const { return "oracle"; }

    void clean_up();

    virtual oracle_statement_backend * make_statement_backend();
    virtual oracle_rowid_backend * make_rowid_backend();
    virtual oracle_blob_backend * make_blob_backend();

    bool get_option_decimals_as_strings() { return decimals_as_strings_; }

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
    virtual oracle_session_backend * make_session(
        connection_parameters const & parameters) const;
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
