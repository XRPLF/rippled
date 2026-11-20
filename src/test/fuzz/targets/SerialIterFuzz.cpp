#include <test/fuzz/FuzzCheck.h>
#include <test/fuzz/FuzzTargets.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xrpl::fuzz {

/**
 * @brief Drive the SerialIter primitives with a fuzzer-chosen sequence of reads.
 *
 * The input is split in half: the first half is a list of opcodes, the second half is
 * the stream the reads consume. Every read either succeeds or throws; either way the
 * iterator's byte accounting must stay consistent.
 */
int
fuzzSerialIter(std::uint8_t const* data, std::size_t size)
{
    static constexpr char const* kName = "serial_iter";

    std::size_t const opCount = size / 2;
    SerialIter sit{data + opCount, size - opCount};
    std::size_t const total = sit.getBytesLeft();

    for (std::size_t i = 0; i < opCount && !sit.empty(); ++i)
    {
        std::size_t const before = sit.getBytesLeft();
        // A read that runs off the end throws; the accounting check below still applies.
        (void)tryOrEmpty([&] {
            switch (data[i] % 12)
            {
                case 0:
                    (void)sit.get8();
                    break;
                case 1:
                    (void)sit.get16();
                    break;
                case 2:
                    (void)sit.get32();
                    break;
                case 3:
                    (void)sit.get64();
                    break;
                case 4:
                    (void)sit.geti64();
                    break;
                case 5:
                    (void)sit.get160();
                    break;
                case 6:
                    (void)sit.get256();
                    break;
                case 7: {
                    int type = 0;
                    int name = 0;
                    sit.getFieldID(type, name);
                    check(type > 0 && type <= 255, kName, "getFieldID type out of range");
                    check(name > 0 && name <= 255, kName, "getFieldID name out of range");
                    break;
                }
                case 8: {
                    auto const vl = sit.getVL();
                    check(vl.size() <= before, kName, "getVL returned more bytes than remained");
                    break;
                }
                case 9: {
                    int const len = sit.getVLDataLength();
                    check(len >= 0, kName, "negative VL length");
                    break;
                }
                case 10:
                    (void)sit.getVLBuffer();
                    break;
                default:
                    sit.skip(data[i] / 12);
                    break;
            }
            return true;
        });
        check(sit.getBytesLeft() <= before, kName, "read moved the iterator backwards");
    }
    check(sit.getBytesLeft() <= total, kName, "iterator reports more bytes than it was given");
    return 0;
}

/**
 * @brief A Payment with every optional field present, serialized twice in a row.
 *
 * Any bytes are valid input, but a real transaction in both halves gives the opcode half
 * and the data half realistic field IDs and length prefixes.
 */
std::vector<Blob>
serialIterSeeds()
{
    STTx const tx(ttPAYMENT, [](STObject& obj) {
        for (auto const& e : TxFormats::getInstance().findByType(ttPAYMENT)->getSOTemplate())
        {
            // Default-style fields are left out: serializing one at its default is an error.
            if (e.style() == SoeOptional)
                obj.makeFieldPresent(e.sField());
        }
    });
    Serializer s;
    s.addRaw(tx.getSerializer().slice());
    s.addRaw(tx.getSerializer().slice());
    return {s.getData()};
}

}  // namespace xrpl::fuzz
