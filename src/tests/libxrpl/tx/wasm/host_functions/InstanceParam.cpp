#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/wasm/ContractContext.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>
#include <tx/wasm/fixtures/ContractLedger.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

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
        ContractHostOptions options{contract.id(), alice.id()};
        options.instanceParameters = std::move(parameters);
        return makeTracingContractHost(std::move(options));
    }
};

TEST_F(InstanceParamImpl, AnIntegerComesBackLittleEndian)
{
    auto const contractHost = host({param(std::uint32_t{0x01020304})});

    expectValue(contractHost->instanceParam(0, STI_UINT32), (Bytes{0x04, 0x03, 0x02, 0x01}));
}

TEST_F(InstanceParamImpl, EveryIntegerWidthIsItsOwnLength)
{
    auto const contractHost = host({
        param(std::uint8_t{0xff}),
        param(std::uint16_t{0xffff}),
        param(std::uint32_t{0xffffffff}),
        param(std::uint64_t{0xffffffffffffffffULL}),
    });

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

// A hash or a blob comes back as its bytes, and an amount, number, issue or currency as its
// serialization without a field header.
TEST_F(InstanceParamImpl, every_wide_type_is_its_bytes_or_its_serialization)
{
    uint128 const h128{"0102030405060708090A0B0C0D0E0F10"};
    uint160 const h160{"0102030405060708090A0B0C0D0E0F1011121314"};
    uint192 const h192{"0102030405060708090A0B0C0D0E0F101112131415161718"};
    uint256 const h256{"0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20"};
    Blob const blob{0xde, 0xad, 0xbe, 0xef};
    STAmount const amount{sfAmount, XRPAmount{1'000}};
    STNumber const number{sfNumber, Number{125, -2}};
    STIssue const issue{sfAsset, Issue{toCurrency("USD"), alice.id()}};
    STCurrency const currency{sfBaseAsset, toCurrency("EUR")};

    auto const contractHost = host({
        param(h128),
        param(h160),
        param(h192),
        param(h256),
        param(blob),
        param(amount),
        param(number),
        param(issue),
        param(currency),
    });

    auto const serialized = [](STBase const& value) {
        Serializer s;
        value.add(s);
        return Bytes{s.peekData().begin(), s.peekData().end()};
    };

    expectValue(contractHost->instanceParam(0, STI_UINT128), (Bytes{h128.begin(), h128.end()}));
    expectValue(contractHost->instanceParam(1, STI_UINT160), (Bytes{h160.begin(), h160.end()}));
    expectValue(contractHost->instanceParam(2, STI_UINT192), (Bytes{h192.begin(), h192.end()}));
    expectValue(contractHost->instanceParam(3, STI_UINT256), (Bytes{h256.begin(), h256.end()}));
    expectValue(contractHost->instanceParam(4, STI_VL), (Bytes{0xde, 0xad, 0xbe, 0xef}));
    expectValue(contractHost->instanceParam(5, STI_AMOUNT), serialized(amount));
    expectValue(contractHost->instanceParam(6, STI_NUMBER), serialized(number));
    expectValue(contractHost->instanceParam(7, STI_ISSUE), serialized(issue));
    expectValue(contractHost->instanceParam(8, STI_CURRENCY), serialized(currency));

    // Asked as another type, each one is refused.
    expectError(contractHost->instanceParam(0, STI_UINT160), HostFunctionError::InvalidParams);
    expectError(contractHost->instanceParam(5, STI_NUMBER), HostFunctionError::InvalidParams);
}

// A parameter that holds no value answers nothing, whatever type the contract names.
TEST_F(InstanceParamImpl, an_empty_parameter_is_refused)
{
    auto const contractHost = host({ParameterValueVec{STData{sfParameterValue}}});

    expectError(contractHost->instanceParam(0, STI_NOTPRESENT), HostFunctionError::InvalidParams);
    expectError(contractHost->instanceParam(0, STI_UINT8), HostFunctionError::InvalidParams);
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

// The node's trace says why the call was refused.
TEST_F(InstanceParamImpl, an_index_past_the_end_is_logged)
{
    expectError(host({})->instanceParam(0, STI_UINT32), HostFunctionError::IndexOutOfBounds);
    EXPECT_NE(logged().find("instanceParam: Index out of bounds"), std::string::npos);
}

}  // namespace xrpl::test
