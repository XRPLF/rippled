//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_CREATE_IPP
#define NUDB_IMPL_CREATE_IPP

#include <nudb/concepts.hpp>
#include <nudb/native_file.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/format.hpp>
#include <algorithm>
#include <cstring>
#include <random>
#include <stdexcept>
#include <utility>

namespace nudb {

namespace detail {

template<class = void>
std::uint64_t
make_uid()
{
    std::random_device rng;
    std::mt19937_64 gen {rng()};
    std::uniform_int_distribution <std::size_t> dist;
    return dist(gen);
}

} // detail

template<class>
std::uint64_t
make_salt()
{
    std::random_device rng;
    std::mt19937_64 gen {rng()};
    std::uniform_int_distribution <std::size_t> dist;
    return dist(gen);
}

template<
    class Hasher,
    class File,
    class... Args
>
void
create(
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    std::uint64_t appnum,
    std::uint64_t salt,
    nsize_t key_size,
    nsize_t blockSize,
    float load_factor,
    error_code& ec,
    Args&&... args)
{
    static_assert(is_File<File>::value,
        "File requirements not met");

    using namespace detail;
    if(key_size < 1)
    {
        ec = error::invalid_key_size;
        return;
    }
    if(blockSize > field<std::uint16_t>::max)
    {
        ec = error::invalid_block_size;
        return;
    }
    if(load_factor <= 0.f || load_factor >= 1.f)
    {
        ec = error::invalid_load_factor;
        return;
    }
    auto const capacity =
        bucket_capacity(blockSize);
    if(capacity < 1)
    {
        ec = error::invalid_block_size;
        return;
    }
    bool edf = false;
    bool ekf = false;
    bool elf = false;
    {
        File df(args...);
        File kf(args...);
        File lf(args...);
        df.create(file_mode::append, dat_path, ec);
        if(ec)
            goto fail;
        edf = true;
        kf.create(file_mode::append, key_path, ec);
        if(ec)
            goto fail;
        ekf = true;
        lf.create(file_mode::append, log_path, ec);
        if(ec)
            goto fail;
        elf = true;
        dat_file_header dh;
        dh.version = currentVersion;
        dh.uid = make_uid();
        dh.appnum = appnum;
        dh.key_size = key_size;

        key_file_header kh;
        kh.version = currentVersion;
        kh.uid = dh.uid;
        kh.appnum = appnum;
        kh.key_size = key_size;
        kh.salt = salt;
        kh.pepper = pepper<Hasher>(salt);
        kh.block_size = blockSize;
        kh.load_factor = std::min<std::size_t>(
            static_cast<std::size_t>(
                65536.0 * load_factor), 65535);
        write(df, dh, ec);
        if(ec)
            goto fail;
        write(kf, kh, ec);
        if(ec)
            goto fail;
        buffer buf{blockSize};
        std::memset(buf.get(), 0, blockSize);
        bucket b(blockSize, buf.get(), empty);
        b.write(kf, blockSize, ec);
        if(ec)
            goto fail;
        // VFALCO Leave log file empty?
        df.sync(ec);
        if(ec)
            goto fail;
        kf.sync(ec);
        if(ec)
            goto fail;
        lf.sync(ec);
        if(ec)
            goto fail;
        // Success
        return;
    }
fail:
    if(edf)
        erase_file(dat_path);
    if(ekf)
        erase_file(key_path);
    if(elf)
        erase_file(log_path);
}

} // nudb

#endif
