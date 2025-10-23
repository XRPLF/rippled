#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Serializer.h>

#include <limits>

namespace ripple {

struct Serializer_test : public beast::unit_test::suite
{
    void
    run() override
    {
        {
            std::initializer_list<std::int32_t> const values = {
                std::numeric_limits<std::int32_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int32_t>::max()};
            for (std::int32_t value : values)
            {
                Serializer s;
                s.add32(value);
                BEAST_EXPECT(s.size() == 4);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti32() == value);
            }
        }
        {
            std::initializer_list<std::int64_t> const values = {
                std::numeric_limits<std::int64_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int64_t>::max()};
            for (std::int64_t value : values)
            {
                Serializer s;
                s.add64(value);
                BEAST_EXPECT(s.size() == 8);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti64() == value);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(Serializer, protocol, ripple);

}  // namespace ripple
