//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Copyright (C) 2011 Gevorg Voskanyan
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_POSTGRESQL_H_INCLUDED
#define SOCI_POSTGRESQL_H_INCLUDED

#ifdef _WIN32
# ifdef SOCI_DLL
#  ifdef SOCI_POSTGRESQL_SOURCE
#   define SOCI_POSTGRESQL_DECL __declspec(dllexport)
#  else
#   define SOCI_POSTGRESQL_DECL __declspec(dllimport)
#  endif // SOCI_POSTGRESQL_SOURCE
# endif // SOCI_DLL
#endif // _WIN32
//
// If SOCI_POSTGRESQL_DECL isn't defined yet define it now
#ifndef SOCI_POSTGRESQL_DECL
# define SOCI_POSTGRESQL_DECL
#endif

#include <soci/soci-backend.h>
#include <libpq-fe.h>
#include <vector>

namespace soci
{

class SOCI_POSTGRESQL_DECL postgresql_soci_error : public soci_error
{
public:
    postgresql_soci_error(std::string const & msg, char const * sqlst);

    std::string sqlstate() const;

    error_category get_error_category() const SOCI_OVERRIDE { return cat_; }

private:
    char sqlstate_[ 5 ];   // not std::string to keep copy-constructor no-throw
    error_category cat_;
};

struct postgresql_session_backend;

namespace details
{

// A class thinly encapsulating PGresult. Its main purpose is to ensure that
// PQclear() is always called, avoiding result memory leaks.
class postgresql_result
{
public:
    // Creates a wrapper for the given, possibly NULL, result. The wrapper
    // object takes ownership of the result object and will call PQclear() on it.
    explicit postgresql_result(
        postgresql_session_backend & sessionBackend,
        PGresult * result)
        : sessionBackend_(sessionBackend)
    {
        init(result);
    }

    // Frees any currently stored result pointer and takes ownership of the
    // given one.
    void reset(PGresult* result = NULL)
    {
        free();
        init(result);
    }

    // Check whether the status is PGRES_COMMAND_OK and throw an exception if
    // it is different. Notice that if the query can return any results,
    // check_for_data() below should be used instead to verify whether anything
    // was returned or not.
    //
    // The provided error message is used only for the exception being thrown
    // and should describe the operation which yielded this result.
    void check_for_errors(char const* errMsg) const;

    // Check whether the status indicates successful query completion, either
    // with the return results (in which case true is returned) or without them
    // (then false is returned). If the status corresponds to an error, throws
    // an exception, just as check_for_errors().
    bool check_for_data(char const* errMsg) const;

    // Implicit conversion to const PGresult: this is somewhat dangerous but
    // allows us to avoid changing the existing code that uses PGresult and
    // avoids the really bad problem with calling PQclear() twice accidentally
    // as this would require a conversion to non-const pointer that we do not
    // provide.
    operator const PGresult*() const { return result_; }

    // Get the associated result (which may be NULL). Unlike the implicit
    // conversion above, this one returns a non-const pointer, so you should be
    // careful to avoid really modifying it.
    PGresult* get_result() const { return result_; }

    // Dtor frees the result.
    ~postgresql_result() { free(); }

private:
    void init(PGresult* result)
    {
        result_ = result;
    }

    void free()
    {
        // Notice that it is safe to call PQclear() with NULL pointer, it
        // simply does nothing in this case.
        PQclear(result_);
    }

    postgresql_session_backend & sessionBackend_;
    PGresult* result_;

    SOCI_NOT_COPYABLE(postgresql_result)
};

} // namespace details

struct postgresql_statement_backend;
struct postgresql_standard_into_type_backend : details::standard_into_type_backend
{
    postgresql_standard_into_type_backend(postgresql_statement_backend & st)
        : statement_(st) {}

    void define_by_pos(int & position,
        void * data, details::exchange_type type) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, bool calledFromFetch,
        indicator * ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    postgresql_statement_backend & statement_;

    void * data_;
    details::exchange_type type_;
    int position_;
};

struct postgresql_vector_into_type_backend : details::vector_into_type_backend
{
    postgresql_vector_into_type_backend(postgresql_statement_backend & st)
        : statement_(st), user_ranges_(true) {}

    void define_by_pos(int & position,
        void * data, details::exchange_type type) SOCI_OVERRIDE
    {
        user_ranges_ = false;
        define_by_pos_bulk(position, data, type, 0, &end_var_);
    }

    void define_by_pos_bulk(int & position,
        void * data, details::exchange_type type,
        std::size_t begin, std::size_t * end) SOCI_OVERRIDE;

    void pre_fetch() SOCI_OVERRIDE;
    void post_fetch(bool gotData, indicator * ind) SOCI_OVERRIDE;

    void resize(std::size_t sz) SOCI_OVERRIDE;
    std::size_t size() SOCI_OVERRIDE; // active size (might be lower than full vector size)
    std::size_t full_size();    // actual size of the user-provided vector

    void clean_up() SOCI_OVERRIDE;

    postgresql_statement_backend & statement_;

    void * data_;
    details::exchange_type type_;
    std::size_t begin_;
    std::size_t * end_;
    std::size_t end_var_;
    bool user_ranges_;
    int position_;
};

struct postgresql_standard_use_type_backend : details::standard_use_type_backend
{
    postgresql_standard_use_type_backend(postgresql_statement_backend & st)
        : statement_(st), position_(0), buf_(NULL) {}

    void bind_by_pos(int & position,
        void * data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;
    void bind_by_name(std::string const & name,
        void * data, details::exchange_type type, bool readOnly) SOCI_OVERRIDE;

    void pre_use(indicator const * ind) SOCI_OVERRIDE;
    void post_use(bool gotData, indicator * ind) SOCI_OVERRIDE;

    void clean_up() SOCI_OVERRIDE;

    postgresql_statement_backend & statement_;

    void * data_;
    details::exchange_type type_;
    int position_;
    std::string name_;
    char * buf_;

private:
    // Allocate buf_ of appropriate size and copy string data into it.
    void copy_from_string(std::string const& s);
};

struct postgresql_vector_use_type_backend : details::vector_use_type_backend
{
    postgresql_vector_use_type_backend(postgresql_statement_backend & st)
        : statement_(st), position_(0) {}

    void bind_by_pos(int & position,
        void * data, details::exchange_type type) SOCI_OVERRIDE
    {
        bind_by_pos_bulk(position, data, type, 0, &end_var_);
    }

    void bind_by_pos_bulk(int & position,
        void * data, details::exchange_type type,
        std::size_t begin, std::size_t * end) SOCI_OVERRIDE;

    void bind_by_name(std::string const & name,
        void * data, details::exchange_type type) SOCI_OVERRIDE
    {
        bind_by_name_bulk(name, data, type, 0, &end_var_);
    }

    void bind_by_name_bulk(const std::string & name,
        void * data, details::exchange_type type,
        std::size_t begin, std::size_t * end) SOCI_OVERRIDE;

    void pre_use(indicator const * ind) SOCI_OVERRIDE;

    std::size_t size() SOCI_OVERRIDE; // active size (might be lower than full vector size)
    std::size_t full_size();    // actual size of the user-provided vector

    void clean_up() SOCI_OVERRIDE;

    postgresql_statement_backend & statement_;

    void * data_;
    details::exchange_type type_;
    std::size_t begin_;
    std::size_t * end_;
    std::size_t end_var_;
    int position_;
    std::string name_;
    std::vector<char *> buffers_;
};

struct postgresql_statement_backend : details::statement_backend
{
    postgresql_statement_backend(postgresql_session_backend & session,
        bool single_row_mode);
    ~postgresql_statement_backend() SOCI_OVERRIDE;

    void alloc() SOCI_OVERRIDE;
    void clean_up() SOCI_OVERRIDE;
    void prepare(std::string const & query,
        details::statement_type stType) SOCI_OVERRIDE;

    exec_fetch_result execute(int number) SOCI_OVERRIDE;
    exec_fetch_result fetch(int number) SOCI_OVERRIDE;

    long long get_affected_rows() SOCI_OVERRIDE;
    int get_number_of_rows() SOCI_OVERRIDE;
    std::string get_parameter_name(int index) const SOCI_OVERRIDE;

    std::string rewrite_for_procedure_call(std::string const & query) SOCI_OVERRIDE;

    int prepare_for_describe() SOCI_OVERRIDE;
    void describe_column(int colNum, data_type & dtype,
        std::string & columnName) SOCI_OVERRIDE;

    postgresql_standard_into_type_backend * make_into_type_backend() SOCI_OVERRIDE;
    postgresql_standard_use_type_backend * make_use_type_backend() SOCI_OVERRIDE;
    postgresql_vector_into_type_backend * make_vector_into_type_backend() SOCI_OVERRIDE;
    postgresql_vector_use_type_backend * make_vector_use_type_backend() SOCI_OVERRIDE;

    postgresql_session_backend & session_;

    bool single_row_mode_;

    details::postgresql_result result_;
    std::string query_;
    details::statement_type stType_;
    std::string statementName_;
    std::vector<std::string> names_; // list of names for named binds

    long long rowsAffectedBulk_; // number of rows affected by the last bulk operation

    int numberOfRows_;  // number of rows retrieved from the server
    int currentRow_;    // "current" row number to consume in postFetch
    int rowsToConsume_; // number of rows to be consumed in postFetch

    bool justDescribed_; // to optimize row description with immediately
                         // following actual statement execution

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

struct postgresql_rowid_backend : details::rowid_backend
{
    postgresql_rowid_backend(postgresql_session_backend & session);

    ~postgresql_rowid_backend() SOCI_OVERRIDE;

    unsigned long value_;
};

struct postgresql_blob_backend : details::blob_backend
{
    postgresql_blob_backend(postgresql_session_backend & session);

    ~postgresql_blob_backend() SOCI_OVERRIDE;

    std::size_t get_len() SOCI_OVERRIDE;

    std::size_t read(std::size_t offset, char * buf,
        std::size_t toRead) SOCI_OVERRIDE;

    std::size_t read_from_start(char * buf, std::size_t toRead,
        std::size_t offset) SOCI_OVERRIDE
    {
        return read(offset, buf, toRead);
    }

    std::size_t write(std::size_t offset, char const * buf,
        std::size_t toWrite) SOCI_OVERRIDE;

    std::size_t write_from_start(const char * buf, std::size_t toWrite,
        std::size_t offset) SOCI_OVERRIDE
    {
        return write(offset, buf, toWrite);
    }

    std::size_t append(char const * buf, std::size_t toWrite) SOCI_OVERRIDE;

    void trim(std::size_t newLen) SOCI_OVERRIDE;

    postgresql_session_backend & session_;

    unsigned long oid_; // oid of the large object
    int fd_;            // descriptor of the large object
};

struct postgresql_session_backend : details::session_backend
{
    postgresql_session_backend(connection_parameters const & parameters,
        bool single_row_mode);

    ~postgresql_session_backend() SOCI_OVERRIDE;

    void connect(connection_parameters const & parameters);

    void begin() SOCI_OVERRIDE;
    void commit() SOCI_OVERRIDE;
    void rollback() SOCI_OVERRIDE;

    void deallocate_prepared_statement(const std::string & statementName);

    bool get_next_sequence_value(session & s,
        std::string const & sequence, long & value) SOCI_OVERRIDE;

    std::string get_dummy_from_table() const SOCI_OVERRIDE { return std::string(); }

    std::string get_backend_name() const SOCI_OVERRIDE { return "postgresql"; }

    void clean_up();

    postgresql_statement_backend * make_statement_backend() SOCI_OVERRIDE;
    postgresql_rowid_backend * make_rowid_backend() SOCI_OVERRIDE;
    postgresql_blob_backend * make_blob_backend() SOCI_OVERRIDE;

    std::string get_next_statement_name();

    int statementCount_;
    bool single_row_mode_;
    PGconn * conn_;
};


struct postgresql_backend_factory : backend_factory
{
    postgresql_backend_factory() {}
    postgresql_session_backend * make_session(
        connection_parameters const & parameters) const SOCI_OVERRIDE;
};

extern SOCI_POSTGRESQL_DECL postgresql_backend_factory const postgresql;

extern "C"
{

// for dynamic backend loading
SOCI_POSTGRESQL_DECL backend_factory const * factory_postgresql();
SOCI_POSTGRESQL_DECL void register_factory_postgresql();

} // extern "C"

} // namespace soci

#endif // SOCI_POSTGRESQL_H_INCLUDED
