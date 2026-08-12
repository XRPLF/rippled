#include <tx/wasm/HostContextFixture.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xrpl::test {

rust::Slice<std::uint8_t const>
HostContextTest::bytesOf(Bytes const& bytes)
{
    return rust::Slice<std::uint8_t const>{bytes.data(), bytes.size()};
}

HostContextTest::OutRegion::OutRegion(std::size_t capacity) : bytes(capacity, kSentinel)
{
}

rust::Slice<std::uint8_t>
HostContextTest::OutRegion::slice()
{
    return rust::Slice<std::uint8_t>{bytes.data(), bytes.size()};
}

bool
HostContextTest::OutRegion::wasWritten() const
{
    return std::ranges::any_of(bytes, [](std::uint8_t b) { return b != kSentinel; });
}

bool
HostContextTest::OutRegion::holds(rust::Slice<std::uint8_t const> expected) const
{
    if (expected.size() > bytes.size())
    {
        return false;
    }

    auto want = std::vector<std::uint8_t>(bytes.size(), kSentinel);
    std::ranges::copy(expected, want.begin());
    return bytes == want;
}

std::string
HostContextTest::logged() const
{
    return sink.messages();
}

}  // namespace xrpl::test
