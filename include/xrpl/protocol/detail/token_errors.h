#pragma once

#include <string>
#include <system_error>
#include <type_traits>

namespace xrpl {
enum class TokenCodecErrc {
    Success = 0,
    InputTooLarge,
    InputTooSmall,
    BadB58Character,
    OutputTooSmall,
    MismatchedTokenType,
    MismatchedChecksum,
    InvalidEncodingChar,
    OverflowAdd,
    Unknown,
};
}  // namespace xrpl

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
    [[nodiscard]] char const*
    name() const noexcept final
    {
        return "TokenCodecError";
    }
    // Return what each enum means in text
    [[nodiscard]] std::string
    message(int c) const final
    {
        switch (static_cast<TokenCodecErrc>(c))
        {
            case TokenCodecErrc::Success:
                return "conversion successful";
            case TokenCodecErrc::InputTooLarge:
                return "input too large";
            case TokenCodecErrc::InputTooSmall:
                return "input too small";
            case TokenCodecErrc::BadB58Character:
                return "bad base 58 character";
            case TokenCodecErrc::OutputTooSmall:
                return "output too small";
            case TokenCodecErrc::MismatchedTokenType:
                return "mismatched token type";
            case TokenCodecErrc::MismatchedChecksum:
                return "mismatched checksum";
            case TokenCodecErrc::InvalidEncodingChar:
                return "invalid encoding char";
            case TokenCodecErrc::Unknown:
            default:
                return "unknown";
        }
    }
};
}  // namespace detail

inline xrpl::detail::TokenCodecErrcCategory const&
tokenCodecErrcCategory()
{
    static xrpl::detail::TokenCodecErrcCategory const kC;
    return kC;
}

inline std::error_code
make_error_code(xrpl::TokenCodecErrc e)
{
    return {static_cast<int>(e), tokenCodecErrcCategory()};
}
}  // namespace xrpl
