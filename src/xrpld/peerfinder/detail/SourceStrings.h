#pragma once

#include <xrpld/peerfinder/detail/Source.h>

#include <memory>

namespace xrpl {
namespace PeerFinder {

/** Provides addresses from a static set of strings. */
class SourceStrings : public Source
{
public:
    explicit SourceStrings() = default;

    using Strings = std::vector<std::string>;

    static std::shared_ptr<Source>
    New(std::string const& name, Strings const& strings);
};

}  // namespace PeerFinder
}  // namespace xrpl
