//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/conditions/impl/error.h>
#include <system_error>
#include <string>
#include <type_traits>

namespace ripple {
namespace cryptoconditions {
namespace detail {

class cryptoconditions_error_category
    : public std::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "cryptoconditions";
    }

    std::string
    message(int ev) const override
    {
        switch (static_cast<error>(ev))
        {
        case error::unsupported_type:
            return "Specification: Requested type not supported.";

        case error::unsupported_subtype:
            return "Specification: Requested subtype not supported.";

        case error::unknown_type:
            return "Specification: Requested type not recognized.";

        case error::unknown_subtype:
            return "Specification: Requested subtypes not recognized.";

        case error::fingerprint_size:
            return "Specification: Incorrect fingerprint size.";

        case error::incorrect_encoding:
            return "Specification: Incorrect encoding.";

        case error::trailing_garbage:
            return "Bad buffer: contains trailing garbage.";

        case error::buffer_empty:
            return "Bad buffer: no data.";

        case error::buffer_overfull:
            return "Bad buffer: overfull.";

        case error::buffer_underfull:
            return "Bad buffer: underfull.";

        case error::malformed_encoding:
            return "Malformed DER encoding.";

        case error::unexpected_tag:
            return "Malformed DER encoding: Unexpected tag.";

        case error::short_preamble:
            return "Malformed DER encoding: Short preamble.";

        case error::long_tag:
            return "Implementation limit: Overlong tag.";

        case error::large_size:
            return "Implementation limit: Large payload.";

        case error::preimage_too_long:
            return "Implementation limit: Specified preimage is too long.";

        case error::generic:
        default:
            return "generic error";
        }
    }

    std::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return std::error_condition{ ev, *this };
    }

    bool
    equivalent(
        int ev,
        std::error_condition const& condition) const noexcept override
    {
        return &condition.category() == this &&
            condition.value() == ev;
    }

    bool
    equivalent(
        std::error_code const& error,
        int ev) const noexcept override
    {
        return &error.category() == this &&
            error.value() == ev;
    }
};

inline
std::error_category const&
get_cryptoconditions_error_category()
{
    static cryptoconditions_error_category const cat{};
    return cat;
}

} // detail

std::error_code
make_error_code(error ev)
{
    return std::error_code {
        static_cast<std::underlying_type<error>::type>(ev),
        detail::get_cryptoconditions_error_category()
    };
}

}
}
