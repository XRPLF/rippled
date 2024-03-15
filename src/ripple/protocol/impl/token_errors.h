//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_TOKEN_ERRORS_H_INCLUDED
#define RIPPLE_PROTOCOL_TOKEN_ERRORS_H_INCLUDED

#include <system_error>

namespace ripple {
enum class TokenCodecErrc {
    success = 0,
    inputTooLarge,
    inputTooSmall,
    badB58Character,
    outputTooSmall,
    mismatchedTokenType,
    mismatchedChecksum,
    invalidEncodingChar,
    unknown,
};
}

namespace std {
template <>
struct is_error_code_enum<ripple::TokenCodecErrc> : true_type
{
};
}  // namespace std

namespace ripple {
namespace detail {
class TokenCodecErrcCategory : public std::error_category
{
public:
    // Return a short descriptive name for the category
    virtual const char*
    name() const noexcept override final
    {
        return "TokenCodecError";
    }
    // Return what each enum means in text
    virtual std::string
    message(int c) const override final
    {
        switch (static_cast<TokenCodecErrc>(c))
        {
            case TokenCodecErrc::success:
                return "conversion successful";
            case TokenCodecErrc::inputTooLarge:
                return "input too large";
            case TokenCodecErrc::inputTooSmall:
                return "input too small";
            case TokenCodecErrc::badB58Character:
                return "bad base 58 character";
            case TokenCodecErrc::outputTooSmall:
                return "output too small";
            case TokenCodecErrc::mismatchedTokenType:
                return "mismatched token type";
            case TokenCodecErrc::mismatchedChecksum:
                return "mismatched checksum";
            case TokenCodecErrc::invalidEncodingChar:
                return "invalid encoding char";
            case TokenCodecErrc::unknown:
                return "unknown";
            default:
                return "unknown";
        }
    }
};
}  // namespace detail

inline const ripple::detail::TokenCodecErrcCategory&
TokenCodecErrcCategory()
{
    static ripple::detail::TokenCodecErrcCategory c;
    return c;
}

inline std::error_code
make_error_code(ripple::TokenCodecErrc e)
{
    return {static_cast<int>(e), TokenCodecErrcCategory()};
}
}  // namespace ripple
#endif  // TOKEN_ERRORS_H_
