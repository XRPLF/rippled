#include <xrpl/basics/Log.h>
#include <xrpl/basics/mulDiv.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/ContractContext.h>

namespace xrpl {

std::vector<ParameterValueVec>
getParameterValueVec(STArray const& functionParameters)
{
    std::vector<ParameterValueVec> param_map;
    for (auto const& param : functionParameters)
    {
        auto const& value = param.getFieldData(sfParameterValue);
        param_map.emplace_back(value);
    }
    return param_map;
}

std::vector<ParameterTypeVec>
getParameterTypeVec(STArray const& functionParameters)
{
    std::vector<ParameterTypeVec> param_map;
    for (auto const& param : functionParameters)
    {
        auto const& type = param.getFieldDataType(sfParameterType);
        param_map.emplace_back(type);
    }
    return param_map;
}

}  // namespace xrpl
