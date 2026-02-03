#pragma once

// TODO: consider moving these to separate files (and figure out the build)

#include <cstdint>
#include <string>
#include <vector>

// WASM binary format constants and helpers for building test modules
namespace wasm_constants {

// Magic + version header
static constexpr uint8_t WASM_HEADER[] = {
    0x00,
    0x61,
    0x73,
    0x6d,  // magic: \0asm
    0x01,
    0x00,
    0x00,
    0x00  // version: 1
};

// Type section: () -> ()
static constexpr uint8_t TYPE_EMPTY_FUNC[] = {0x01, 0x04, 0x01, 0x60, 0x00, 0x00};

// Function section: one function using type 0
static constexpr uint8_t FUNC_TYPE0[] = {0x03, 0x02, 0x01, 0x00};

// Export section: export func 0 as "finish"
static constexpr uint8_t EXPORT_FINISH[] = {0x07, 0x0a, 0x01, 0x06, 'f', 'i', 'n', 'i', 's', 'h', 0x00, 0x00};

// Empty function body: 0 locals, end
static constexpr uint8_t EMPTY_BODY[] = {0x00, 0x0b};

// Data segment offset: i32.const 0, end
static constexpr uint8_t DATA_OFFSET_ZERO[] = {0x41, 0x00, 0x0b};

// Section IDs
static constexpr uint8_t SECTION_MEMORY = 0x05;
static constexpr uint8_t SECTION_CODE = 0x0a;
static constexpr uint8_t SECTION_DATA = 0x0b;

// Instructions
static constexpr uint8_t INSTR_NOP = 0x01;
static constexpr uint8_t INSTR_END = 0x0b;

// Fill byte for data section bloat
static constexpr uint8_t DATA_FILL_BYTE = 0xEE;

// Generator for WASM module with large code section (many NOPs)
std::vector<uint8_t>
generateCodeBlob(uint32_t num_instructions);

// Generator for WASM module with large data section
std::vector<uint8_t>
generateDataBlob(uint32_t data_size);

}  // namespace wasm_constants

extern std::string const ledgerSqnWasmHex;

extern std::string const allHostFunctionsWasmHex;

extern std::string const deepRecursionHex;

extern std::string const fibWasmHex;

extern std::string const b58WasmHex;

extern std::string const sha512PureWasmHex;

extern std::string const hfPerfTest;

extern std::string const allKeyletsWasmHex;

extern std::string const codecovTestsWasmHex;

extern std::string const floatTestsWasmHex;

extern std::string const float0Hex;

extern std::string const disabledFloatHex;

extern std::string const memoryPointerAtLimitHex;
extern std::string const memoryPointerOverLimitHex;
extern std::string const memoryOffsetOverLimitHex;
extern std::string const memoryEndOfWordOverLimitHex;
extern std::string const memoryGrow0To1PageHex;
extern std::string const memoryGrow1To0PageHex;
extern std::string const memoryLastByteOf8MBHex;
extern std::string const memoryGrow1MoreThan8MBHex;
extern std::string const memoryGrow0MoreThan8MBHex;
extern std::string const memoryInit1MoreThan8MBHex;
extern std::string const memoryNegativeAddressHex;

extern std::string const table64ElementsHex;
extern std::string const table65ElementsHex;
extern std::string const table2TablesHex;
extern std::string const table0ElementsHex;
extern std::string const tableUintMaxHex;

extern std::string const proposalMutableGlobalHex;
extern std::string const proposalGcStructNewHex;
extern std::string const proposalMultiValueHex;
extern std::string const proposalSignExtHex;
extern std::string const proposalFloatToIntHex;
extern std::string const proposalBulkMemoryHex;
extern std::string const proposalRefTypesHex;
extern std::string const proposalTailCallHex;
extern std::string const proposalExtendedConstHex;
extern std::string const proposalMultiMemoryHex;
extern std::string const proposalCustomPageSizesHex;
extern std::string const proposalMemory64Hex;
extern std::string const proposalWideArithmeticHex;

extern std::string const trapDivideBy0Hex;
extern std::string const trapIntOverflowHex;
extern std::string const trapUnreachableHex;
extern std::string const trapNullCallHex;
extern std::string const trapFuncSigMismatchHex;

extern std::string const wasiGetTimeHex;
extern std::string const wasiPrintHex;

extern std::string const badMagicNumberHex;
extern std::string const badVersionNumberHex;
extern std::string const lyingHeaderHex;
extern std::string const neverEndingNumberHex;
extern std::string const vectorLieHex;
extern std::string const sectionOrderingHex;
extern std::string const ghostPayloadHex;
extern std::string const junkAfterSectionHex;
extern std::string const invalidSectionIdHex;
extern std::string const localVariableBombHex;

extern std::string const infiniteLoopWasmHex;
extern std::string const startLoopHex;

extern std::string const badAllocHex;
extern std::string const badAlignHex;

extern std::string const updateDataWasmHex;
