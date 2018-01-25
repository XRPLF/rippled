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
#include <soci/soci-backend.h>
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

    ~firebird_soci_error() throw() SOCI_OVERRIDE {};

    std::vector<ISC_STATUS> status_;
};

enum BuffersType
{
    eStandard, eVector
};

struct firebird_blob_backend;
struct firebird_statement_backend;
struct firebird_standard_into_type_backend : details::standard_into_type_backend
{
    firebird_standard_into_type_backend(firebird_statement_backend &st)
        : statement_(st), data_(NULL), type_(), position_(0), buf_(NULL), indISCHolder_(0)
    {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch,
        indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    firebird_statement_backend &statement_;
    virtual void exchangeData();

    void *data_;
    details::exchange_type type_;
    int position_;

    char *buf_;
    short indISCHolder_;

private:
    // Copy contents of a BLOB (represented by its id) in buf_ into the given
    // string.
    void copy_from_blob(std::string& out);
};

struct firebird_vector_into_type_backend : details::vector_into_type_backend
{
    firebird_vector_into_type_backend(firebird_statement_backend &st)
        : statement_(st), data_(NULL), type_(), position_(0), buf_(NULL), indISCHolder_(0)
    {}

    void define_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

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
        : statement_(st), data_(NULL), type_(), position_(0), buf_(NULL), indISCHolder_(0),
          blob_(NULL)
    {}

    void bind_by_pos(int &position,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator *ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    firebird_statement_backend &statement_;
    virtual void exchangeData();

    void *data_;
    details::exchange_type type_;
    int position_;

    char *buf_;
    short indISCHolder_;

private:
    // Allocate a temporary blob, fill it with the data from the provided
    // string and copy its ID into buf_.
    void copy_to_blob(const std::string& in);

    // This is used for types mapping to CLOB.
    firebird_blob_backend* blob_;
};

struct firebird_vector_use_type_backend : details::vector_use_type_backend
{
    firebird_vector_use_type_backend(firebird_statement_backend &st)
        : statement_(st), data_(NULL), type_(), position_(0), buf_(NULL), indISCHolder_(0)
    {}

    void bind_by_pos(int &position,
        void *data, details::exchange_type type) SOCI_OVERRIDE;
    void bind_by_name(std::string const &name,
        void *data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_use(indicator const *ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

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

    firebird_standard_into_type_backend * make_into_type_backend() SOCI_OVERRIDE;
    firebird_standard_use_type_backend * make_use_type_backend() SOCI_OVERRIDE;
    firebird_vector_into_type_backend * make_vector_into_type_backend() SOCI_OVERRIDE;
    firebird_vector_use_type_backend * make_vector_use_type_backend() SOCI_OVERRIDE;

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
    virtual void prepareSQLDA(XSQLDA ** sqldap, short size = 10);
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

struct firebird_blob_backend : details::blob_backend
{
    firebird_blob_backend(firebird_session_backend &session);

    ~firebird_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;
    std::size_t read(std::size_t offset, char *buf,
        std::size_t toRead) SOCI_OVERRIDE;
    std::size_t write(std::size_t offset, char const *buf,
        std::size_t toWrite) SOCI_OVERRIDE;
    std::size_t append(char const *buf, std::size_t toWrite) SOCI_OVERRIDE;
    void trim(std::size_t newLen) SOCI_OVERRIDE;

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

    ~firebird_session_backend() SOCI_OVERRIDE;

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    bool get_next_sequence_value(session & s,
        std::string const & sequence, long & value) SOCI_OVERRIDE;

    std::string get_dummy_from_table() const SOCI_OVERRIDE { return "rdb$database"; }

    std::string get_backend_name() const SOCI_OVERRIDE { return "firebird"; }

    void cleanUp();

    firebird_statement_backend * make_statement_backend() SOCI_OVERRIDE;
    details::rowid_backend* make_rowid_backend() SOCI_OVERRIDE;
    firebird_blob_backend * make_blob_backend() SOCI_OVERRIDE;

    bool get_option_decimals_as_strings() { return decimals_as_strings_; }

    // Returns the pointer to the current transaction handle, starting a new
    // transaction if necessary.
    //
    // The returned pointer should
    isc_tr_handle* current_transaction();

    isc_db_handle dbhp_;

private:
    isc_tr_handle trhp_;
    bool decimals_as_strings_;
};

struct firebird_backend_factory : backend_factory
{
    firebird_backend_factory() {}
    firebird_session_backend * make_session(
        connection_parameters const & parameters) const SOCI_OVERRIDE;
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
