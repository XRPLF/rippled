//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_BASIC_STORE_HPP
#define NUDB_BASIC_STORE_HPP

#include <nudb/file.hpp>
#include <nudb/type_traits.hpp>
#include <nudb/detail/cache.hpp>
#include <nudb/detail/gentex.hpp>
#include <nudb/detail/mutex.hpp>
#include <nudb/detail/pool.hpp>
#include <boost/optional.hpp>
#include <chrono>
#include <mutex>
#include <thread>

namespace nudb {

/** A high performance, insert-only key/value database for SSDs.

    To create a database first call the @ref create
    free function. Then construct a @ref basic_store and
    call @ref open on it:

    @code
        error_code ec;
        create<xxhasher>(
            "db.dat", "db.key", "db.log",
                1, make_salt(), 8, 4096, 0.5f, ec);
        basic_store<xxhasher, native_file> db;
        db.open("db.dat", "db.key", "db.log", ec);
    @endcode

    @tparam Hasher The hash function to use. This type
    must meet the requirements of @b Hasher.

    @tparam File The type of File object to use. This type
    must meet the requirements of @b File.
*/
template<class Hasher, class File>
class basic_store
{
public:
    using hash_type = Hasher;
    using file_type = File;

private:
    using clock_type =
        std::chrono::steady_clock;

    using time_point =
        typename clock_type::time_point;

    struct state
    {
        File df;
        File kf;
        File lf;
        path_type dp;
        path_type kp;
        path_type lp;
        Hasher hasher;
        detail::pool p0;
        detail::pool p1;
        detail::cache c1;
        detail::key_file_header kh;

        std::size_t rate = 0;
        time_point when = clock_type::now();

        state(state const&) = delete;
        state& operator=(state const&) = delete;

        state(state&&) = default;
        state& operator=(state&&) = default;

        state(File&& df_, File&& kf_, File&& lf_,
            path_type const& dp_, path_type const& kp_,
                path_type const& lp_,
                    detail::key_file_header const& kh_);
    };

    bool open_ = false;

    // Use optional because some
    // members cannot be default-constructed.
    //
    boost::optional<state> s_;      // State of an open database

    std::size_t frac_;              // accumulates load
    std::size_t thresh_;            // split threshold
    nbuck_t buckets_;               // number of buckets
    nbuck_t modulus_;               // hash modulus

    std::mutex u_;                  // serializes insert()
    detail::gentex g_;
    boost::shared_mutex m_;
    std::thread t_;
    std::condition_variable_any cv_;

    error_code ec_;
    std::atomic<bool> ecb_;         // `true` when ec_ set

    std::size_t dataWriteSize_;
    std::size_t logWriteSize_;

public:
    /** Default constructor.

        A default constructed database is initially closed.
    */
    basic_store() = default;

    /// Copy constructor (disallowed)
    basic_store(basic_store const&) = delete;

    /// Copy assignment (disallowed)
    basic_store& operator=(basic_store const&) = delete;

    /** Destroy the database.

        Files are closed, memory is freed, and data that has not been
        committed is discarded. To ensure that all inserted data is
        written, it is necessary to call @ref close before destroying
        the @ref basic_store.

        This function ignores errors returned by @ref close; to receive
        those errors it is necessary to call @ref close before the
        @ref basic_store is destroyed.
    */
    ~basic_store();

    /** Returns `true` if the database is open.

        @par Thread safety

        Safe to call concurrently with any function
        except @ref open or @ref close.
    */
    bool
    is_open() const
    {
        return open_;
    }

    /** Return the path to the data file.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function
        except @ref open or @ref close.

        @return The data file path.
    */
    path_type const&
    dat_path() const;

    /** Return the path to the key file.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function
        except @ref open or @ref close.

        @return The key file path.
    */
    path_type const&
    key_path() const;

    /** Return the path to the log file.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function
        except @ref open or @ref close.

        @return The log file path.
    */
    path_type const&
    log_path() const;

    /** Return the appnum associated with the database.

        This is an unsigned 64-bit integer associated with the
        database and defined by the application. It is set
        once when the database is created in a call to
        @ref create.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function
        except @ref open or @ref close.

        @return The appnum.
    */
    std::uint64_t
    appnum() const;

    /** Return the key size associated with the database.

        The key size is defined by the application when the
        database is created in a call to @ref create. The
        key size cannot be changed on an existing database.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function
        except @ref open or @ref close.

        @return The size of keys in the database.
    */
    std::size_t
    key_size() const;

    /** Return the block size associated with the database.

        The block size is defined by the application when the
        database is created in a call to @ref create or when a
        key file is regenerated in a call to @ref rekey. The
        block size cannot be changed on an existing key file.
        Instead, a new key file may be created with a different
        block size.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function
        except @ref open or @ref close.

        @return The size of blocks in the key file.
    */
    std::size_t
    block_size() const;

    /** Close the database.

        All data is committed before closing.

        If an error occurs, the database is still closed.

        @par Requirements

        The database must be open.

        @par Thread safety

        Not thread safe. The caller is responsible for
        ensuring that no other member functions are
        called concurrently.

        @param ec Set to the error, if any occurred.
    */
    void
    close(error_code& ec);

    /** Open a database.

        The database identified by the specified data, key, and
        log file paths is opened. If a log file is present, the
        recovery mechanism is invoked to restore database integrity
        before the function returns.

        @par Requirements

        The database must be not be open.

        @par Thread safety

        Not thread safe. The caller is responsible for
        ensuring that no other member functions are
        called concurrently.

        @param dat_path The path to the data file.

        @param key_path The path to the key file.

        @param log_path The path to the log file.

        @param ec Set to the error, if any occurred.

        @param args Optional arguments passed to @b File constructors.

    */
    template<class... Args>
    void
    open(
        path_type const& dat_path,
        path_type const& key_path,
        path_type const& log_path,
        error_code& ec,
        Args&&... args);

    /** Fetch a value.

        The function checks the database for the specified
        key, and invokes the callback if it is found. If
        the key is not found, `ec` is set to @ref error::key_not_found.
        If any other errors occur, `ec` is set to the
        corresponding error.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function except
        @ref close.

        @note If the implementation encounters an error while
        committing data to the database, this function will
        immediately return with `ec` set to the error which
        occurred. All subsequent calls to @ref fetch will
        return the same error until the database is closed.

        @param key A pointer to a memory buffer of at least
        @ref key_size() bytes, containing the key to be searched
        for.

        @param callback A function which will be called with the
        value data if the fetch is successful. The equivalent
        signature must be:
        @code
        void callback(
            void const* buffer, // A buffer holding the value
            std::size_t size    // The size of the value in bytes
        );
        @endcode
        The buffer provided to the callback remains valid
        until the callback returns, ownership is not transferred.

        @param ec Set to the error, if any occurred.
    */
    template<class Callback>
    void
    fetch(void const* key, Callback && callback, error_code& ec);

    /** Insert a value.

        This function attempts to insert the specified key/value
        pair into the database. If the key already exists,
        `ec` is set to @ref error::key_exists. If an error
        occurs, `ec` is set to the corresponding error.

        @par Requirements

        The database must be open.

        @par Thread safety

        Safe to call concurrently with any function except
        @ref close.

        @note If the implementation encounters an error while
        committing data to the database, this function will
        immediately return with `ec` set to the error which
        occurred. All subsequent calls to @ref insert will
        return the same error until the database is closed.

        @param key A buffer holding the key to be inserted. The
        size of the buffer should be at least the `key_size`
        associated with the open database.

        @param data A buffer holding the value to be inserted.

        @param bytes The size of the buffer holding the value
        data. This value must be greater than 0 and no more
        than 0xffffffff.

        @param ec Set to the error, if any occurred.
    */
    void
    insert(void const* key, void const* data,
        nsize_t bytes, error_code& ec);

private:
    template<class Callback>
    void
    fetch(detail::nhash_t h, void const* key,
        detail::bucket b, Callback && callback, error_code& ec);

    bool
    exists(detail::nhash_t h, void const* key,
        detail::shared_lock_type* lock, detail::bucket b, error_code& ec);

    void
    split(detail::bucket& b1, detail::bucket& b2,
        detail::bucket& tmp, nbuck_t n1, nbuck_t n2,
            nbuck_t buckets, nbuck_t modulus,
                detail::bulk_writer<File>& w, error_code& ec);

    detail::bucket
    load(nbuck_t n, detail::cache& c1,
        detail::cache& c0, void* buf, error_code& ec);

    void
    commit(detail::unique_lock_type& m,
        std::size_t& work, error_code& ec);

    void
    run();
};

} // nudb

#include <nudb/impl/basic_store.ipp>

#endif
