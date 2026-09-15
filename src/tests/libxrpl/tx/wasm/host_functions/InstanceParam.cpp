#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace xrpl::test {

// instance_param — the parameters a contract instance was created with, each answered as
// the type the contract asks for.
//
// Note the byte order: an integer parameter comes back **little-endian**, unlike the
// canonical big-endian serialization `get_data_object_field` answers with. The two
// conventions sit side by side in the ABI, so both are pinned.
struct InstanceParamImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host(std::vector<ParameterValueVec> parameters)
    {
        return makeContractHost(
            {.contractAccount = contract.id(),
             .caller = alice.id(),
             .instanceParameters = std::move(parameters)});
    }
};

TEST_F(InstanceParamImpl, AnIntegerComesBackLittleEndian)
{
    auto const contractHost = host({param(std::uint32_t{0x01020304})});

    expectValue(contractHost->instanceParam(0, STI_UINT32), (Bytes{0x04, 0x03, 0x02, 0x01}));
}

TEST_F(InstanceParamImpl, EveryIntegerWidthIsItsOwnLength)
{
    auto const contractHost = host(
        {param(std::uint8_t{0xff}),
         param(std::uint16_t{0xffff}),
         param(std::uint32_t{0xffffffff}),
         param(std::uint64_t{0xffffffffffffffffULL})});

    expectValue(contractHost->instanceParam(0, STI_UINT8), Bytes(1, 0xff));
    expectValue(contractHost->instanceParam(1, STI_UINT16), Bytes(2, 0xff));
    expectValue(contractHost->instanceParam(2, STI_UINT32), Bytes(4, 0xff));
    expectValue(contractHost->instanceParam(3, STI_UINT64), Bytes(8, 0xff));
}

TEST_F(InstanceParamImpl, AnAccountIsItsTwentyBytes)
{
    auto const contractHost = host({param(alice.id())});

    expectValue(
        contractHost->instanceParam(0, STI_ACCOUNT), (Bytes{alice.id().begin(), alice.id().end()}));
}

// The type is what the contract claims the parameter holds, so a parameter of another type
// is a malformed request rather than a converted value.
TEST_F(InstanceParamImpl, AParameterOfAnotherTypeIsRefused)
{
    auto const contractHost = host({param(std::uint32_t{7})});

    expectError(contractHost->instanceParam(0, STI_UINT16), HostFunctionError::InvalidParams);
}

TEST_F(InstanceParamImpl, AnIndexPastTheEndIsOutOfBounds)
{
    auto const contractHost = host({param(std::uint32_t{7})});

    expectError(contractHost->instanceParam(1, STI_UINT32), HostFunctionError::IndexOutOfBounds);
}

TEST_F(InstanceParamImpl, AnInstanceWithNoParametersHasNothingAtIndexZero)
{
    expectError(host({})->instanceParam(0, STI_UINT32), HostFunctionError::IndexOutOfBounds);
}

// The instance's parameters and the call's are separate tables, so an instance parameter is
// not reachable as a function parameter.
TEST_F(InstanceParamImpl, TheCallsParametersAreADifferentTable)
{
    auto const contractHost = host({param(std::uint32_t{7})});

    expectError(contractHost->functionParam(0, STI_UINT32), HostFunctionError::IndexOutOfBounds);
}

}  // namespace xrpl::test
