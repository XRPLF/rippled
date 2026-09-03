#include <xrpl/protocol/digest.h>

#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

#include <string>

namespace xrpl::test {

struct Sha512HalfImpl : RealHostFixture
{
};

TEST_F(Sha512HalfImpl, LogsMessageAndData)
{
    static constexpr auto data = std::string_view{"hello world"};
    auto const result = makeHost()->computeSha512HalfHash({data.data(), data.size()});
    expectValue(result, sha512Half(Slice{data.data(), data.size()}));
}

}  // namespace xrpl::test
