#include <tx/wasm/fixtures/BytesHelpers.h>

#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <vector>

namespace xrpl::test {

Bytes
bytesOfSteps(std::vector<std::int32_t> const& steps)
{
    Bytes bytes;
    bytes.reserve(steps.size() * sizeof(std::int32_t));
    for (auto const step : steps)
    {
        auto const wire = bytesOfScalar(step);
        bytes.insert(bytes.end(), wire.begin(), wire.end());
    }
    return bytes;
}

}  // namespace xrpl::test
