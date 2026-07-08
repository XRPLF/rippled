#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/digest.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace xrpl::test {

static std::vector<char> const data = []() {
    std::vector<char> strV(pow(10, 5));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(32, 127);
    for (size_t index = 0; index < strV.size(); ++index)
    {
        strV[index] = static_cast<char>(dis(gen));
    }
    return strV;
}();

TEST(OpenSSL, SingleHashFullSlice)
{
    Slice const s{data.data(), data.size()};
    [[maybe_unused]] auto hash = sha512Half(s);
}

TEST(OpenSSL, MultihashAllSlices)
{
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        Slice s(&data[i], data.size() - i);
        [[maybe_unused]] auto hash = sha512Half(s);
    }
}

}  // namespace xrpl::test
