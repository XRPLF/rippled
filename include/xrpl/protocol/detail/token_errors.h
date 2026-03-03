#pragma once

#include <system_error>

namespace xrpl {
enum class TokenCodecErrc {
    success = 0,
    inputTooLarge,
    inputTooSmall,
    badB58Character,
    outputTooSmall,
    mismatchedTokenType,
    mismatchedChecksum,
    invalidEncodingChar,
    overflowAdd,
    unknown,
};
}

namespace std {
template <>
struct is_error_code_enum<xrpl::TokenCodecErrc> : true_type
{
};
}  // namespace std

namespace xrpl {
namespace detail {
class TokenCodecErrcCategory : public std::error_category
{
public:
    // Return a short descriptive name for the category
    virtual char const*
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

inline xrpl::detail::TokenCodecErrcCategory const&
TokenCodecErrcCategory()
{
    static xrpl::detail::TokenCodecErrcCategory c;
    return c;
}

inline std::error_code
make_error_code(xrpl::TokenCodecErrc e)
{
    return {static_cast<int>(e), TokenCodecErrcCategory()};
}
}  // namespace xrpl
