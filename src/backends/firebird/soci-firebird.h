//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
//

#ifndef SOCI_FIREBIRD_H_INCLUDED
#define SOCI_FIREBIRD_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_FIREBIRD_SOURCE
#   define SOCI_FIREBIRD_DECL __declspec(dllexport)
#  else
#   define SOCI_FIREBIRD_DECL __declspec(dllimport)
#  endif // SOCI_DLL
# endif // SOCI_FIREBIRD_SOURCE
#endif // _WIN32

//
// If SOCI_FIREBIRD_DECL isn't defined yet define it now
#ifndef SOCI_FIREBIRD_DECL
# define SOCI_FIREBIRD_DECL
#endif

#ifdef _WIN32
#include <ciso646> // To understand and/or/not on MSVC9
#endif
#include <soci-backend.h>
#include <ibase.h> // FireBird
#include <cstdlib>
#include <vector>
#include <string>

namespace soci
{

std::size_t const stat_size = 20;

// size of buffer for error messages. All examples use this value.
// Anyone knows, where it is stated that 512 bytes is enough ?
std::size_t const SOCI_FIREBIRD_ERRMSG = 512;

class SOCI_FIREBIRD_DECL firebird_soci_error : public soci_error
{
public:
    firebird_soci_error(std::string const & msg,
        ISC_STATUS const * status = 0);

    ~firebird_soci_error() throw() {};

    std::vector<ISC_STATUS> status_;
};

enum BuffersType
{
    eStandard, eVector
};

struct firebird_statement_backend;
struct firebird_standard_into_type_backend : details::standard_into_type_backend
{
    firebird_standard_into_type_backend(firebird_statement_backend &st)
        : statement_(st), buf_(NULL)
    {}

    virtual void define_by_pos(int &position,
        void *data, details::exchange_type type);

    virtual void pre_fetch();
    virtual void post_fetch(bool gotData, bool calledFromFetch,
        indicator *ind);

    virtual void clean_up();

    firebird_statement_backend &statement_;
    virtual void exchangeData();

    void *data_;
    details::exchange_type type_;
    int position_;

    char *buf_;
    short indISCHolder_;
};

struct firebird_vector_into_type_backend : details::vector_into_type_backend
{
    firebird_vector_into_type_backend(firebird_statement_backend &st)
        : statement_(st), buf_(NULL)
    {}

    virtual void define_by_pos(int &position,
        void *data, details::exchange_type type);

    virtual void pre_fetch();
    virtual void post_fetch(bool gotData, indicator *ind);

    virtual void resize(std::size_t sz);
    virtual std::size_t size();

    virtual void clean_up();

    firebird_statement_backend &statement_;
    virtual void exchangeData(std::size_t row);

    void *data_;
    details::exchange_type type_;
    int position_;

    char *buf_;
    short indISCHolder_;
};

struct firebird_standard_use_type_backend : details::standard_use_type_backend
{
    firebird_standard_use_type_backend(firebird_statement_backend &st)
        : statement_(st), buf_(NULL), indISCHolder_(0)
    {}

    virtual void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly);
    virtual void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly);

    virtual void pre_use(indicator const *ind);
    virtual void post_use(bool gotData, indicator *ind);

    virtual void clean_up();

    firebird_statement_backend &statement_;
    virtual void exchangeData();

    void *data_;
    details::exchange_type type_;
    int position_;

    char *buf_;
    short indISCHolder_;
};

struct firebird_vector_use_type_backend : details::vector_use_type_backend
{
    firebird_vector_use_type_backend(firebird_statement_backend &st)
        : statement_(st), inds_(NULL), buf_(NULL), indISCHolder_(0)
    {}

    virtual void bind_by_pos(int &position,
        void *data, details::exchange_type type);
    virtual void bind_by_name(std::string const &name,
        void *data, details::exchange_type type);

    virtual void pre_use(indicator const *ind);

    virtual std::size_t size();

    virtual void clean_up();

    firebird_statement_backend &statement_;
    virtual void exchangeData(std::size_t row);

    void *data_;
    details::exchange_type type_;
    int position_;
    indicator const *inds_;

    char *buf_;
    short indISCHolder_;
};

struct firebird_session_backend;
struct firebird_statement_backend : details::statement_backend
{
    firebird_statement_backend(firebird_session_backend &session);

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

    virtual firebird_standard_into_type_backend * make_into_type_backend();
    virtual firebird_standard_use_type_backend * make_use_type_backend();
    virtual firebird_vector_into_type_backend * make_vector_into_type_backend();
    virtual firebird_vector_use_type_backend * make_vector_use_type_backend();

    firebird_session_backend &session_;

    isc_stmt_handle stmtp_;
    XSQLDA * sqldap_;
    XSQLDA * sqlda2p_;

    bool boundByName_;
    bool boundByPos_;

    friend struct firebird_vector_into_type_backend;
    friend struct firebird_standard_into_type_backend;
    friend struct firebird_vector_use_type_backend;
    friend struct firebird_standard_use_type_backend;

protected:
    int rowsFetched_;
    bool endOfRowSet_;

    long long rowsAffectedBulk_; // number of rows affected by the last bulk operation

    virtual void exchangeData(bool gotData, int row);
    virtual void prepareSQLDA(XSQLDA ** sqldap, int size = 10);
    virtual void rewriteQuery(std::string const & query,
        std::vector<char> & buffer);
    virtual void rewriteParameters(std::string const & src,
        std::vector<char> & dst);

    BuffersType intoType_;
    BuffersType useType_;

    std::vector<std::vector<indicator> > inds_;
    std::vector<void*> intos_;
    std::vector<void*> uses_;

    // named parameters
    std::map <std::string, int> names_;

    bool procedure_;
};

struct firebird_rowid_backend : details::rowid_backend
{
    firebird_rowid_backend(firebird_session_backend &session);

    ~firebird_rowid_backend();
};

struct firebird_blob_backend : details::blob_backend
{
    firebird_blob_backend(firebird_session_backend &session);

    ~firebird_blob_backend();

    virtual std::size_t get_len();
    virtual std::size_t read(std::size_t offset, char *buf,
        std::size_t toRead);
    virtual std::size_t write(std::size_t offset, char const *buf,
        std::size_t toWrite);
    virtual std::size_t append(char const *buf, std::size_t toWrite);
    virtual void trim(std::size_t newLen);

    firebird_session_backend &session_;

    virtual void save();
    virtual void assign(ISC_QUAD const & bid)
    {
        cleanUp();

        bid_ = bid;
        from_db_ = true;
    }

    // BLOB id from in database
    ISC_QUAD bid_;

    // BLOB id was fetched from database (true)
    // or this is new BLOB
    bool from_db_;

    // BLOB handle
    isc_blob_handle bhp_;

protected:

    virtual void open();
    virtual long getBLOBInfo();
    virtual void load();
    virtual void writeBuffer(std::size_t offset, char const * buf,
        std::size_t toWrite);
    virtual void cleanUp();

    // buffer for BLOB data
    std::vector<char> data_;

    bool loaded_;
    long max_seg_size_;
};

struct firebird_session_backend : details::session_backend
{
    firebird_session_backend(connection_parameters const & parameters);

    ~firebird_session_backend();

    virtual void begin();
    virtual void commit();
    virtual void rollback();

    virtual bool get_next_sequence_value(session & s,
        std::string const & sequence, long & value);

    virtual std::string get_backend_name() const { return "firebird"; }

    void cleanUp();

    virtual firebird_statement_backend * make_statement_backend();
    virtual firebird_rowid_backend * make_rowid_backend();
    virtual firebird_blob_backend * make_blob_backend();

    virtual void setDPBOption(int const option, std::string const & value);

    bool get_option_decimals_as_strings() { return decimals_as_strings_; }

    isc_db_handle dbhp_;
    isc_tr_handle trhp_;
    std::string dpb_;
    bool decimals_as_strings_;
};

struct firebird_backend_factory : backend_factory
{
    firebird_backend_factory() {}
    virtual firebird_session_backend * make_session(
        connection_parameters const & parameters) const;
};

extern SOCI_FIREBIRD_DECL firebird_backend_factory const firebird;

extern "C"
{

// for dynamic backend loading
SOCI_FIREBIRD_DECL backend_factory const * factory_firebird();
SOCI_FIREBIRD_DECL void register_factory_firebird();

} // extern "C"

} // namespace soci

#endif // SOCI_FIREBIRD_H_INCLUDED
