#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/nodestore/detail/varint.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xrpl::NodeStore::tests {

class varint_test : public beast::unit_test::Suite
{
public:
    void
    testVarints(std::vector<std::size_t> vv)
    {
        testcase("encode, decode");
        for (auto const v : vv)
        {
            std::array<std::uint8_t, varint_traits<std::size_t>::kMax> vi{};
            auto const n0 = writeVarint(vi.data(), v);
            expect(n0 > 0, "write error");
            expect(n0 == sizeVarint(v), "size error");
            std::size_t v1 = 0;
            auto const n1 = readVarint(vi.data(), n0, v1);
            expect(n1 == n0, "read error");
            expect(v == v1, "wrong value");
        }
    }

    void
    run() override
    {
        testVarints(
            {0,
             1,
             2,
             126,
             127,
             128,
             253,
             254,
             255,
             16127,
             16128,
             16129,
             0xff,
             0xffff,
             0xffffffff,
             0xffffffffffffUL,
             0xffffffffffffffffUL});
    }
};

BEAST_DEFINE_TESTSUITE(varint, nodestore, xrpl);

}  // namespace xrpl::NodeStore::tests
