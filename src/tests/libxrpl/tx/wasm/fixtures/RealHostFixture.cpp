#include <tx/wasm/fixtures/RealHostFixture.h>

#include <xrpl/protocol/Keylet.h>
#include <xrpl/tx/wasm/HostFailure.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <expected>

namespace xrpl::test {

void
expectKeyletMatches(std::expected<Bytes, HostFailure> const& result, Keylet const& expected)
{
    expectValue(result, RealHostFixture::toBytes(expected.key));
}

}  // namespace xrpl::test
