#pragma once

#include <system_error>

namespace xrpl::cryptoconditions {

enum class Error {
    Generic = 1,
    UnsupportedType,
    UnsupportedSubtype,
    UnknownType,
    UnknownSubtype,
    FingerprintSize,
    IncorrectEncoding,
    TrailingGarbage,
    BufferEmpty,
    BufferOverfull,
    BufferUnderfull,
    MalformedEncoding,
    ShortPreamble,
    UnexpectedTag,
    LongTag,
    LargeSize,
    PreimageTooLong
};

std::error_code
make_error_code(Error ev);

}  // namespace xrpl::cryptoconditions

namespace std {

template <>
struct is_error_code_enum<xrpl::cryptoconditions::Error>
{
    explicit is_error_code_enum() = default;

    static bool const value = true;  // NOLINT(readability-identifier-naming)
};

}  // namespace std
