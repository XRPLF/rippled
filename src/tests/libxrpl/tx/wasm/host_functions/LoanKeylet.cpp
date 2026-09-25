#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct LoanKeyletImpl : RealHostFixture
{
};

TEST_F(LoanKeyletImpl, MatchesLoanKeyletFunction)
{
    Bytes const loanBrokerIdBytes{0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b,
                                  0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                                  0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70};
    uint256 const loanBrokerId = uint256::fromVoid(loanBrokerIdBytes.data());

    expectKeyletMatches(
        makeHost()->loanKeylet(loanBrokerId, 1u),
        keylet::loan(loanBrokerId, SeqProxy::rawSequence(1u)));
}

TEST_F(LoanKeyletImpl, InvalidLoanBrokerId)
{
    expectError(makeHost()->loanKeylet(uint256{}, 1u), HostFunctionError::InvalidParams);
}

}  // namespace xrpl::test
