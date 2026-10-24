#include <xrpl/tx/wasm/ContractContext.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>

#include <vector>

namespace xrpl {

std::vector<ParameterValueVec>
getParameterValueVec(STArray const& functionParameters)
{
    std::vector<ParameterValueVec> paramMap;
    for (auto const& param : functionParameters)
    {
        auto const& value = param.getFieldData(sfParameterValue);
        paramMap.emplace_back(value);
    }
    return paramMap;
}

std::vector<ParameterTypeVec>
getParameterTypeVec(STArray const& functionParameters)
{
    std::vector<ParameterTypeVec> paramMap;
    for (auto const& param : functionParameters)
    {
        auto const& type = param.getFieldDataType(sfParameterType);
        paramMap.emplace_back(type);
    }
    return paramMap;
}

}  // namespace xrpl
