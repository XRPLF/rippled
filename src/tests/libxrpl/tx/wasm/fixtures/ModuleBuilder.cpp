#include <tx/wasm/fixtures/ModuleBuilder.h>

#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <cstdint>
#include <string_view>

namespace xrpl::test {
namespace {

// Section ids, from the binary format's fixed table.
constexpr std::uint8_t kSectionType = 0x01;
constexpr std::uint8_t kSectionFunction = 0x03;
constexpr std::uint8_t kSectionMemory = 0x05;
constexpr std::uint8_t kSectionExport = 0x07;
constexpr std::uint8_t kSectionCode = 0x0A;
constexpr std::uint8_t kSectionData = 0x0B;

constexpr std::uint8_t kOpcodeNop = 0x01;
constexpr std::uint8_t kOpcodeEnd = 0x0B;
constexpr std::uint8_t kOpcodeI32Const = 0x41;

constexpr std::uint8_t kTypeI32 = 0x7F;
constexpr std::uint8_t kTypeFunc = 0x60;

constexpr std::uint32_t kPageBytes = 65'536;

// Anything that isn't obviously zero-filled is 0xEE, so a dump of a failing module shows at
// a glance which bytes are padding.
constexpr std::uint8_t kDataFillByte = 0xEE;

void
appendU32Leb(Bytes& out, std::uint32_t value)
{
    do
    {
        auto byte = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0U)
        {
            byte |= 0x80;
        }
        out.push_back(byte);
    } while (value != 0U);
}

void
appendSection(Bytes& out, std::uint8_t section, Bytes const& payload)
{
    out.push_back(section);
    appendU32Leb(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(std::end(out), std::begin(payload), std::end(payload));
}

// A function body: no locals, `code`, `end` — prefixed by its own byte length.
void
appendBody(Bytes& out, Bytes const& code)
{
    auto body = Bytes{0x00};  // local declaration count
    body.insert(std::end(body), std::begin(code), std::end(code));
    body.push_back(kOpcodeEnd);

    appendU32Leb(out, static_cast<std::uint32_t>(body.size()));
    out.insert(std::end(out), std::begin(body), std::end(body));
}

Bytes
header()
{
    return Bytes{0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};  // "\0asm", version 1
}

// Two types: `() -> ()` for filler functions, `() -> i32` for the entry point.
constexpr std::uint8_t kTypeVoid = 0;
constexpr std::uint8_t kTypeReturnsI32 = 1;

void
appendTypeSection(Bytes& out)
{
    auto payload = Bytes{0x02};  // two types
    payload.insert(std::end(payload), {kTypeFunc, 0x00, 0x00});
    payload.insert(std::end(payload), {kTypeFunc, 0x00, 0x01, kTypeI32});
    appendSection(out, kSectionType, payload);
}

// `fillerCount` functions of type `() -> ()`, then the entry point of type `() -> i32`.
void
appendFunctionSection(Bytes& out, std::uint32_t fillerCount)
{
    auto payload = Bytes{};
    appendU32Leb(payload, fillerCount + 1);
    payload.insert(std::end(payload), fillerCount, kTypeVoid);
    payload.push_back(kTypeReturnsI32);
    appendSection(out, kSectionFunction, payload);
}

// Export the entry point, which is the last function declared.
void
appendExportSection(Bytes& out, std::uint32_t fillerCount, bool exportMemory)
{
    auto payload = Bytes{};
    appendU32Leb(payload, exportMemory ? 2 : 1);

    if (exportMemory)
    {
        static constexpr auto kMemory = std::string_view{"memory"};
        appendU32Leb(payload, static_cast<std::uint32_t>(kMemory.size()));
        payload.insert(std::end(payload), std::begin(kMemory), std::end(kMemory));
        payload.push_back(0x02);  // export kind: memory
        payload.push_back(0x00);  // memory index
    }

    appendU32Leb(payload, static_cast<std::uint32_t>(escrowFunctionName.size()));
    payload.insert(std::end(payload), std::begin(escrowFunctionName), std::end(escrowFunctionName));
    payload.push_back(0x00);  // export kind: function
    appendU32Leb(payload, fillerCount);

    appendSection(out, kSectionExport, payload);
}

// `i32.const 1` — a completed run that the transactor reads as success.
Bytes
entryPointCode()
{
    return Bytes{kOpcodeI32Const, 0x01};
}

}  // namespace

Bytes
codeHeavyModule(std::uint32_t instructionCount)
{
    // One filler function holding every `nop`, plus the entry point.
    constexpr std::uint32_t kFillerCount = 1;

    auto out = header();
    appendTypeSection(out);
    appendFunctionSection(out, kFillerCount);
    appendExportSection(out, kFillerCount, /*exportMemory*/ false);

    auto codePayload = Bytes{};
    appendU32Leb(codePayload, kFillerCount + 1);
    appendBody(codePayload, Bytes(instructionCount, kOpcodeNop));
    appendBody(codePayload, entryPointCode());

    appendSection(out, kSectionCode, codePayload);
    return out;
}

Bytes
dataHeavyModule(std::uint32_t dataBytes)
{
    auto out = header();
    appendTypeSection(out);
    appendFunctionSection(out, /*fillerCount*/ 0);

    auto memoryPayload = Bytes{0x01, 0x00};  // one memory, minimum-only limits
    appendU32Leb(memoryPayload, (dataBytes + kPageBytes - 1) / kPageBytes);
    appendSection(out, kSectionMemory, memoryPayload);

    appendExportSection(out, /*fillerCount*/ 0, /*exportMemory*/ true);

    auto codePayload = Bytes{0x01};  // one function body
    appendBody(codePayload, entryPointCode());
    appendSection(out, kSectionCode, codePayload);

    auto dataPayload = Bytes{0x01, 0x00};  // one segment, memory 0
    dataPayload.insert(std::end(dataPayload), {kOpcodeI32Const, 0x00, kOpcodeEnd});  // offset 0
    appendU32Leb(dataPayload, dataBytes);
    dataPayload.insert(std::end(dataPayload), dataBytes, kDataFillByte);
    appendSection(out, kSectionData, dataPayload);

    return out;
}

}  // namespace xrpl::test
