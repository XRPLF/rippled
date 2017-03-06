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

#ifndef RIPPLE_CONDITIONS_ERROR_H
#define RIPPLE_CONDITIONS_ERROR_H

#include <system_error>
#include <string>

namespace ripple {
namespace cryptoconditions {

enum class error
{
    generic = 1,
    unsupported_type,
    unsupported_subtype,
    unknown_type,
    unknown_subtype,
    fingerprint_size,
    incorrect_encoding,
    trailing_garbage,
    buffer_empty,
    buffer_overfull,
    buffer_underfull,
    malformed_encoding,
    short_preamble,
    unexpected_tag,
    long_tag,
    large_size,
    preimage_too_long,
};

std::error_code
make_error_code(error ev);

} // cryptoconditions
} // ripple

namespace std
{

template<>
struct is_error_code_enum<ripple::cryptoconditions::error>
{
    static bool const value = true;
};

} // std

#endif
