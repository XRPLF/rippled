#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/rfc2616.h>

#include <boost/beast/core/string_type.hpp>
#include <boost/iterator/function_output_iterator.hpp>
#include <boost/regex/v5/regbase.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace xrpl {

/** The list of protocol versions we speak and we prefer to use.

    @note The list must be sorted in strictly ascending order (and so
          it may not contain any duplicates!)
*/

constexpr ProtocolVersion const kSupportedProtocolList[]{
    {2, 1},
    {2, 2},
};

// This ugly construct ensures that supportedProtocolList is sorted in strictly
// ascending order and doesn't contain any duplicates.
// FIXME: With C++20 we can use std::is_sorted with an appropriate comparator
static_assert(
    []() constexpr -> bool {
        auto const len =
            std::distance(std::begin(kSupportedProtocolList), std::end(kSupportedProtocolList));

        // There should be at least one protocol we're willing to speak.
        if (len == 0)
            return false;

        // A list with only one entry is, by definition, sorted so we don't
        // need to check it.
        if (len != 1)
        {
            for (auto i = 0; i != len - 1; ++i)
            {
                if (kSupportedProtocolList[i] >= kSupportedProtocolList[i + 1])
                    return false;
            }
        }

        return true;
    }(),
    "The list of supported protocols isn't properly sorted.");

std::string
to_string(ProtocolVersion const& p)
{
    return "XRPL/" + std::to_string(p.first) + "." + std::to_string(p.second);
}

std::vector<ProtocolVersion>
parseProtocolVersions(boost::beast::string_view const& value)
{
    static boost::regex const kRE(
        "^"                        // start of line
        "XRPL/"                    // The string "XRPL/"
        "([2-9]|(?:[1-9][0-9]+))"  // a number (greater than 2 with no leading
                                   // zeroes)
        "\\."                      // a period
        "(0|(?:[1-9][0-9]*))"      // a number (no leading zeroes unless exactly
                                   // zero)
        "$"                        // The end of the string
        ,
        boost::regex_constants::optimize);

    std::vector<ProtocolVersion> result;

    for (auto const& s : beast::rfc2616::splitCommas(value))
    {
        boost::smatch m;

        if (boost::regex_match(s, m, kRE))
        {
            std::uint16_t major = 0;
            std::uint16_t minor = 0;
            if (!beast::lexicalCastChecked(major, std::string(m[1])))
                continue;

            if (!beast::lexicalCastChecked(minor, std::string(m[2])))
                continue;

            auto const proto = makeProtocol(major, minor);

            // This is an extra sanity check: we check that the protocol we just
            // decoded corresponds to the token we were parsing.
            if (to_string(proto) == s)
                result.push_back(makeProtocol(major, minor));
        }
    }

    // We guarantee that the returned list is sorted and contains no duplicates:
    std::ranges::sort(result);
    auto const uniq = std::ranges::unique(result);
    result.erase(uniq.begin(), uniq.end());

    return result;
}

std::optional<ProtocolVersion>
negotiateProtocolVersion(std::vector<ProtocolVersion> const& versions)
{
    std::optional<ProtocolVersion> result;

    // The protocol version we want to negotiate is the largest item in the
    // intersection of the versions supported by us and the peer. Since the
    // output of std::set_intersection is sorted, that item is always going
    // to be the last one. So we get a little clever and avoid the need for
    // a container:
    std::function<void(ProtocolVersion const&)> const pickVersion =
        [&result](ProtocolVersion const& v) { result = v; };

    std::ranges::set_intersection(
        versions, kSupportedProtocolList, boost::make_function_output_iterator(pickVersion));

    return result;
}

std::optional<ProtocolVersion>
negotiateProtocolVersion(boost::beast::string_view const& versions)
{
    auto const them = parseProtocolVersions(versions);

    return negotiateProtocolVersion(them);
}

std::string const&
supportedProtocolVersions()
{
    static std::string const kSupported = []() {
        std::string ret;
        for (auto const& v : kSupportedProtocolList)
        {
            if (!ret.empty())
                ret += ", ";
            ret += to_string(v);
        }

        return ret;
    }();

    return kSupported;
}

bool
isProtocolSupported(ProtocolVersion const& v)
{
    return std::end(kSupportedProtocolList) != std::ranges::find(kSupportedProtocolList, v);
}

}  // namespace xrpl
