#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFormats.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

#ifndef XRPL_FUZZ_CORPUS_DIR
#error "XRPL_FUZZ_CORPUS_DIR must name the src/test/fuzz/corpus directory"
#endif

namespace xrpl::fuzz {

/**
 * @brief Regenerate the seed inputs in src/test/fuzz/corpus.
 *
 * Manual suite: run with `xrpld --unittest=FuzzSeeds` after changing a serialized format,
 * then commit the new seed files. Seeds give libFuzzer valid, fully populated examples,
 * so it starts from inputs the parser accepts.
 */
class FuzzSeeds_test : public beast::unit_test::Suite
{
    std::filesystem::path const root_{XRPL_FUZZ_CORPUS_DIR};

    void
    write(std::string const& target, std::string const& name, Serializer const& s)
    {
        auto const dir = root_ / target;
        std::filesystem::create_directories(dir);
        std::ofstream out(dir / ("seed-" + name + ".bin"), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<char const*>(s.data()), static_cast<std::streamsize>(s.size()));
        BEAST_EXPECT(out.good());
    }

    // Make every optional field present with its default value. Default-style fields are
    // left out: serializing one that holds its default value is a logic error.
    static void
    addOptionalFields(STObject& obj, SOTemplate const& format)
    {
        for (auto const& e : format)
        {
            if (e.style() == SoeOptional)
                obj.makeFieldPresent(e.sField());
        }
    }

    void
    writeValidations()
    {
        testcase("validations");
        auto const [pk, sk] = generateKeyPair(KeyType::Secp256k1, generateSeed("fuzz seeds"));

        for (bool const full : {false, true})
        {
            STValidation const val(
                NetClock::time_point{NetClock::duration{1}},
                pk,
                sk,
                calcNodeID(pk),
                [&](STValidation& v) {
                    v.setFieldH256(sfLedgerHash, UInt256{2});
                    v.setFieldU32(sfLedgerSequence, 3);
                    if (full)
                    {
                        v.setFlag(kVfFullValidation);
                        v.setFieldU32(sfLoadFee, 256);
                        v.setFieldU64(sfCookie, 4);
                    }
                });
            write("st_validation", full ? "full" : "partial", val.getSerializer());
        }
    }

    // The SerialIter target reads the first half of its input as opcodes, so any bytes
    // work; a transaction gives it realistic length prefixes and field IDs to chew on.
    void
    writeSerialIter()
    {
        testcase("serial iter");
        STTx const tx(ttPAYMENT, [](STObject& obj) {
            addOptionalFields(obj, TxFormats::getInstance().findByType(ttPAYMENT)->getSOTemplate());
        });
        Serializer s;
        s.addRaw(tx.getSerializer().slice());
        s.addRaw(tx.getSerializer().slice());
        write("serial_iter", "payment-twice", s);
    }

public:
    void
    run() override
    {
        writeValidations();
        writeSerialIter();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(FuzzSeeds, fuzz, xrpl);

}  // namespace xrpl::fuzz
