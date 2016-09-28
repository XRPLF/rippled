//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_ERROR_IPP
#define NUDB_IMPL_ERROR_IPP

namespace nudb {

inline
error_category const&
nudb_category()
{
    struct cat_t : public error_category
    {
        char const*
        name() const noexcept override
        {
            return "nudb";
        }

        std::string
        message(int ev) const override
        {
            switch(static_cast<error>(ev))
            {
            case error::success:
                return "the operation completed successfully";

            case error::key_not_found:
                return "key not found";

            case error::key_exists:
                return "key already exists";

            case error::short_read:
                return "short read";

            case error::log_file_exists:
                return "a log file exists";

            case error::no_key_file:
                return "no key file";

            case error::too_many_buckets:
                return "too many buckets";

            case error::not_data_file:
                return "not a data file";

            case error::not_key_file:
                return "not a key file";

            case error::not_log_file:
                return "not a log file";

            case error::different_version:
                return "different version";

            case error::invalid_key_size:
                return "invalid key size";
            
            case error::invalid_block_size:
                return "invalid block size";

            case error::short_key_file:
                return "short key file";

            case error::short_bucket:
                return "short bucket";

            case error::short_spill:
                return "short spill";

            case error::short_data_record:
                return "short data record";

            case error::short_value:
                return "short value";

            case error::hash_mismatch:
                return "hash mismatch";

            case error::invalid_load_factor:
                return "invalid load factor";

            case error::invalid_capacity:
                return "invalid capacity";

            case error::invalid_bucket_count:
                return "invalid bucket count";

            case error::invalid_bucket_size:
                return "invalid_bucket_size";

            case error::incomplete_data_file_header:
                return "incomplete data file header";

            case error::incomplete_key_file_header:
                return "incomplete key file header";

            case error::invalid_log_record:
                return "invalid log record";

            case error::invalid_log_spill:
                return "invalid spill in log";

            case error::invalid_log_offset:
                return "invalid offset in log";
                
            case error::invalid_log_index:
                return "invalid index in log";

            case error::invalid_spill_size:
                return "invalid size in spill";

            case error::uid_mismatch:
                return "uid mismatch";

            case error::appnum_mismatch:
                return "appnum mismatch";

            case error::key_size_mismatch:
                return "key size mismatch";

            case error::salt_mismatch:
                return "salt mismatch";

            case error::pepper_mismatch:
                return "pepper mismatch";

            case error::block_size_mismatch:
                return "block size mismatch";

            case error::orphaned_value:
                return "orphaned value";

            case error::missing_value:
                return "missing value";

            case error::size_mismatch:
                return "size mismatch";

            case error::duplicate_value:
                return "duplicate value";

            default:
                return "nudb error";
            }
        }

        error_condition
        default_error_condition(int ev) const noexcept override
        {
            return error_condition{ev, *this};
        }

        bool
        equivalent(int ev,
            error_condition const& ec) const noexcept override
        {
            return ec.value() == ev && &ec.category() == this;
        }

        bool
        equivalent(error_code const& ec, int ev) const noexcept override
        {
            return ec.value() == ev && &ec.category() == this;
        }
    };
    static cat_t const cat{};
    return cat;
}

} // nudb

#endif
