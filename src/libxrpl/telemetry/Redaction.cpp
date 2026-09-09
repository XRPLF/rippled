#include <xrpl/telemetry/Redaction.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace xrpl::telemetry {

std::string
redactAccount(std::string_view addr)
{
    // Empty in, empty out: nothing to hash and no token to emit.
    if (addr.empty())
        return {};

    // sha512Half yields a uint256; to_string renders it as uppercase hex.
    // Keep the first 16 chars (64 bits) — enough to correlate spans while
    // staying non-reversible — and lowercase them for a stable token.
    auto const digest = sha512Half(Slice(addr.data(), addr.size()));
    std::string token = to_string(digest).substr(0, 16);
    std::ranges::transform(
        token, token.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return token;
}

}  // namespace xrpl::telemetry
