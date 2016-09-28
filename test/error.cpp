//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/error.hpp>

#include <beast/unit_test/suite.hpp>

namespace nudb {
namespace test {

class error_test : public beast::unit_test::suite
{
public:
    void check(char const* name, error ev)
    {
        auto const ec = make_error_code(ev);
        BEAST_EXPECT(std::string{ec.category().name()} == name);
        BEAST_EXPECT(! ec.message().empty());
        BEAST_EXPECT(std::addressof(ec.category()) ==
            std::addressof(nudb_category()));
        BEAST_EXPECT(nudb_category().equivalent(static_cast<int>(ev),
            ec.category().default_error_condition(static_cast<int>(ev))));
        BEAST_EXPECT(nudb_category().equivalent(
            ec, static_cast<int>(ev)));
    }

    void run() override
    {
        nudb_category().message(0);
        nudb_category().message(99999);
        check("nudb", error::success);
        check("nudb", error::key_not_found);
        check("nudb", error::key_exists);
        check("nudb", error::short_read);
        check("nudb", error::log_file_exists);
        check("nudb", error::no_key_file);
        check("nudb", error::too_many_buckets);
        check("nudb", error::not_data_file);
        check("nudb", error::not_key_file);
        check("nudb", error::not_log_file);
        check("nudb", error::different_version);
        check("nudb", error::invalid_key_size);
        check("nudb", error::invalid_block_size);
        check("nudb", error::short_key_file);
        check("nudb", error::short_bucket);
        check("nudb", error::short_spill);
        check("nudb", error::short_data_record);
        check("nudb", error::short_value);
        check("nudb", error::hash_mismatch);
        check("nudb", error::invalid_load_factor);
        check("nudb", error::invalid_capacity);
        check("nudb", error::invalid_bucket_count);
        check("nudb", error::invalid_bucket_size);
        check("nudb", error::incomplete_data_file_header);
        check("nudb", error::incomplete_key_file_header);
        check("nudb", error::invalid_log_record);
        check("nudb", error::invalid_log_spill);
        check("nudb", error::invalid_log_offset);
        check("nudb", error::invalid_log_index);
        check("nudb", error::invalid_spill_size);
        check("nudb", error::uid_mismatch);
        check("nudb", error::appnum_mismatch);
        check("nudb", error::key_size_mismatch);
        check("nudb", error::salt_mismatch);
        check("nudb", error::pepper_mismatch);
        check("nudb", error::block_size_mismatch);
        check("nudb", error::orphaned_value);
        check("nudb", error::missing_value);
        check("nudb", error::size_mismatch);
        check("nudb", error::duplicate_value);
    }
};

BEAST_DEFINE_TESTSUITE(error, test, nudb);

} // test
} // nudb

