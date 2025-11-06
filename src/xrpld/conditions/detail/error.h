#ifndef XRPL_CONDITIONS_ERROR_H
#define XRPL_CONDITIONS_ERROR_H

#include <system_error>

namespace ripple {
namespace cryptoconditions {

enum class error {
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
    preimage_too_long
};

std::error_code
make_error_code(error ev);

}  // namespace cryptoconditions
}  // namespace ripple

namespace std {

template <>
struct is_error_code_enum<ripple::cryptoconditions::error>
{
    explicit is_error_code_enum() = default;

    static bool const value = true;
};

}  // namespace std

#endif
