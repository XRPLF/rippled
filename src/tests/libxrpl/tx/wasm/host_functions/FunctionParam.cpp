#include <xrpl/protocol/SField.h>
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

// function_param — the parameters this call was given. Same answers as `instance_param`
// over a different table, which is the only thing that distinguishes them.
struct FunctionParamImpl : ContractHostFixture
{
    Account const alice = fund("alice");
    Account const contract = fund("contract");

    ContractHost
    host(std::vector<ParameterValueVec> parameters)
    {
        ContractHostOptions options{contract.id(), alice.id()};
        options.functionParameters = std::move(parameters);
        return makeTracingContractHost(std::move(options));
    }
};

TEST_F(FunctionParamImpl, AnIntegerComesBackLittleEndian)
{
    auto const contractHost = host({param(std::uint32_t{0x01020304})});

    expectValue(contractHost->functionParam(0, STI_UINT32), (Bytes{0x04, 0x03, 0x02, 0x01}));
}

TEST_F(FunctionParamImpl, EachIndexAnswersItsOwnParameter)
{
    auto const contractHost =
        host({param(std::uint8_t{1}), param(std::uint8_t{2}), param(std::uint8_t{3})});

    expectValue(contractHost->functionParam(0, STI_UINT8), Bytes{1});
    expectValue(contractHost->functionParam(1, STI_UINT8), Bytes{2});
    expectValue(contractHost->functionParam(2, STI_UINT8), Bytes{3});
}

TEST_F(FunctionParamImpl, AParameterOfAnotherTypeIsRefused)
{
    auto const contractHost = host({param(std::uint32_t{7})});

    expectError(contractHost->functionParam(0, STI_UINT8), HostFunctionError::InvalidParams);
}

TEST_F(FunctionParamImpl, AnIndexPastTheEndIsOutOfBounds)
{
    auto const contractHost = host({param(std::uint32_t{7})});

    expectError(contractHost->functionParam(1, STI_UINT32), HostFunctionError::IndexOutOfBounds);
}

TEST_F(FunctionParamImpl, TheInstancesParametersAreADifferentTable)
{
    auto const contractHost = host({param(std::uint32_t{7})});

    expectError(contractHost->instanceParam(0, STI_UINT32), HostFunctionError::IndexOutOfBounds);
}

// The node's trace says why the call was refused.
TEST_F(FunctionParamImpl, an_index_past_the_end_is_logged)
{
    expectError(host({})->functionParam(0, STI_UINT32), HostFunctionError::IndexOutOfBounds);
    EXPECT_NE(logged().find("functionParam: Index out of bounds"), std::string::npos);
}

}  // namespace xrpl::test
