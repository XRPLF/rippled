#include <xrpl/conditions/detail/error.h>

#include <xrpl/basics/safe_cast.h>

#include <string>
#include <system_error>
#include <type_traits>

namespace xrpl::cryptoconditions {
namespace detail {

class CryptoconditionsErrorCategory : public std::error_category
{
public:
    explicit CryptoconditionsErrorCategory() = default;

    [[nodiscard]] char const*
    name() const noexcept override
    {
        return "cryptoconditions";
    }

    [[nodiscard]] std::string
    message(int ev) const override
    {
        switch (safeCast<Error>(ev))
        {
            case Error::UnsupportedType:
                return "Specification: Requested type not supported.";

            case Error::UnsupportedSubtype:
                return "Specification: Requested subtype not supported.";

            case Error::UnknownType:
                return "Specification: Requested type not recognized.";

            case Error::UnknownSubtype:
                return "Specification: Requested subtypes not recognized.";

            case Error::FingerprintSize:
                return "Specification: Incorrect fingerprint size.";

            case Error::IncorrectEncoding:
                return "Specification: Incorrect encoding.";

            case Error::TrailingGarbage:
                return "Bad buffer: contains trailing garbage.";

            case Error::BufferEmpty:
                return "Bad buffer: no data.";

            case Error::BufferOverfull:
                return "Bad buffer: overfull.";

            case Error::BufferUnderfull:
                return "Bad buffer: underfull.";

            case Error::MalformedEncoding:
                return "Malformed DER encoding.";

            case Error::UnexpectedTag:
                return "Malformed DER encoding: Unexpected tag.";

            case Error::ShortPreamble:
                return "Malformed DER encoding: Short preamble.";

            case Error::LongTag:
                return "Implementation limit: Overlong tag.";

            case Error::LargeSize:
                return "Implementation limit: Large payload.";

            case Error::PreimageTooLong:
                return "Implementation limit: Specified preimage is too long.";

            case Error::Generic:
            default:
                return "generic error";
        }
    }

    [[nodiscard]] std::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return std::error_condition{ev, *this};
    }

    [[nodiscard]] bool
    equivalent(int ev, std::error_condition const& condition) const noexcept override
    {
        return &condition.category() == this && condition.value() == ev;
    }

    [[nodiscard]] bool
    equivalent(std::error_code const& error, int ev) const noexcept override
    {
        return &error.category() == this && error.value() == ev;
    }
};

inline std::error_category const&
getCryptoconditionsErrorCategory()
{
    static CryptoconditionsErrorCategory const kCat{};
    return kCat;
}

}  // namespace detail

std::error_code
make_error_code(Error ev)
{
    return std::error_code{
        safeCast<std::underlying_type_t<Error>>(ev), detail::getCryptoconditionsErrorCategory()};
}

}  // namespace xrpl::cryptoconditions
