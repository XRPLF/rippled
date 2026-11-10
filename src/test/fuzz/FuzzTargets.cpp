#include <test/fuzz/FuzzTargets.h>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace xrpl::fuzz {

namespace {

constexpr std::array kTargets{
    FuzzTarget{.name = "serial_iter", .run = &fuzzSerialIter},
    FuzzTarget{.name = "st_validation", .run = &fuzzSTValidation},
};

}  // namespace

std::span<FuzzTarget const>
fuzzTargets()
{
    return kTargets;
}

FuzzTarget const*
findFuzzTarget(std::string_view name)
{
    auto const it =
        std::ranges::find_if(kTargets, [name](FuzzTarget const& t) { return t.name == name; });
    return it == kTargets.end() ? nullptr : &*it;
}

}  // namespace xrpl::fuzz
