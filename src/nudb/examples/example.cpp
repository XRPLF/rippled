//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <nudb/nudb.hpp>
#include <cstddef>
#include <cstdint>

int main()
{
    using namespace nudb;
    std::size_t constexpr N = 1000;
    using key_type = std::uint32_t;
    error_code ec;
    auto const dat_path = "db.dat";
    auto const key_path = "db.key";
    auto const log_path = "db.log";
    create<xxhasher>(
        dat_path, key_path, log_path,
        1,
        make_salt(),
        sizeof(key_type),
        block_size("."),
        0.5f,
        ec);
    store db;
    db.open(dat_path, key_path, log_path, ec);
    char data = 0;
    // Insert
    for(key_type i = 0; i < N; ++i)
        db.insert(&i, &data, sizeof(data), ec);
    // Fetch
    for(key_type i = 0; i < N; ++i)
        db.fetch(&i,
            [&](void const* buffer, std::size_t size)
        {
            // do something with buffer, size
        }, ec);
    db.close(ec);
    erase_file(dat_path);
    erase_file(key_path);
    erase_file(log_path);
}
