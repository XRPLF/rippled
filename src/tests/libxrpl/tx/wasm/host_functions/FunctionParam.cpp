#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractHostFixture.h>

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
        return makeContractHost(
            {.contractAccount = contract.id(),
             .caller = alice.id(),
             .functionParameters = std::move(parameters)});
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

}  // namespace xrpl::test
