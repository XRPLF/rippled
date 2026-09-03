#pragma once

#include <cstdint>
#include <string>
#include <vector>

// WASM binary format constants and helpers for building test modules
namespace wasm_constants {

// Magic + version header
uint8_t const kWasmHeader[] = {
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
uint8_t const kTypeEmptyFunc[] = {0x01, 0x04, 0x01, 0x60, 0x00, 0x00};

// Function section: one function using type 0
uint8_t const kFuncTypE0[] = {0x03, 0x02, 0x01, 0x00};

// Export section: export func 0 as "escrow_finish"
uint8_t const kExportFinish[] = {
    0x07,
    0x11,
    0x01,
    0x0d,
    'e',
    's',
    'c',
    'r',
    'o',
    'w',
    '_',
    'f',
    'i',
    'n',
    'i',
    's',
    'h',
    0x00,
    0x00};

// Empty function body: 0 locals, end
uint8_t const kEmptyBody[] = {0x00, 0x0b};

// Data segment offset: i32.const 0, end
uint8_t const kDataOffsetZero[] = {0x41, 0x00, 0x0b};

// Section IDs
uint8_t const kSectionMemory = 0x05;
uint8_t const kSectionCode = 0x0a;
uint8_t const kSectionData = 0x0b;

// Instructions
uint8_t const kInstrNop = 0x01;
uint8_t const kInstrEnd = 0x0b;

// Fill byte for data section bloat
uint8_t const kDataFillByte = 0xEE;

// Generator for WASM module with large code section (many NOPs)
std::vector<uint8_t>
generateCodeBlob(uint32_t numInstructions);

// Generator for WASM module with large data section
std::vector<uint8_t>
generateDataBlob(uint32_t dataSize);

}  // namespace wasm_constants

extern std::string const kLedgerSqnWasmHex;
extern std::string const kAllHostFunctionsWasmHex;
extern std::string const kAllKeyletsWasmHex;
extern std::string const kCodecovTestsWasmHex;

extern std::string const kFibWasmHex;

extern std::string const kFloatTestsWasmHex;
extern std::string const kFloat0Hex;
extern std::string const kDisabledFloatHex;

extern std::string const kMemoryPointerAtLimitHex;
extern std::string const kMemoryPointerOverLimitHex;
extern std::string const kMemoryOffsetOverLimitHex;
extern std::string const kMemoryEndOfWordOverLimitHex;
extern std::string const kMemoryGrow0To1PageHex;
extern std::string const kMemoryGrow1To0PageHex;
extern std::string const kMemoryLastByteOf8MbHex;
extern std::string const kMemoryGrow1MoreThan8MbHex;
extern std::string const kMemoryGrow0MoreThan8MbHex;
extern std::string const kMemoryInit1MoreThan8MbHex;
extern std::string const kMemoryNegativeAddressHex;

extern std::string const kTable64ElementsHex;
extern std::string const kTable65ElementsHex;
extern std::string const kTable2TablesHex;
extern std::string const kTable0ElementsHex;
extern std::string const kTableUintMaxHex;

extern std::string const kProposalMutableGlobalHex;
extern std::string const kProposalGcStructNewHex;
extern std::string const kProposalMultiValueHex;
extern std::string const kProposalSignExtHex;
extern std::string const kProposalFloatToIntHex;
extern std::string const kProposalBulkMemoryHex;
extern std::string const kProposalRefTypesHex;
extern std::string const kProposalTailCallHex;
extern std::string const kProposalExtendedConstHex;
extern std::string const kProposalMultiMemoryHex;
extern std::string const kProposalCustomPageSizesHex;
extern std::string const kProposalMemory64Hex;
extern std::string const kProposalWideArithmeticHex;

extern std::string const kTrapDivideBy0Hex;
extern std::string const kTrapIntOverflowHex;
extern std::string const kTrapUnreachableHex;
extern std::string const kTrapNullCallHex;
extern std::string const kTrapFuncSigMismatchHex;

extern std::string const kWasiGetTimeHex;
extern std::string const kWasiPrintHex;

extern std::string const kBadMagicNumberHex;
extern std::string const kBadVersionNumberHex;
extern std::string const kLyingHeaderHex;
extern std::string const kNeverEndingNumberHex;
extern std::string const kVectorLieHex;
extern std::string const kSectionOrderingHex;
extern std::string const kGhostPayloadHex;
extern std::string const kJunkAfterSectionHex;
extern std::string const kInvalidSectionIdHex;
extern std::string const kLocalVariableBombHex;

extern std::string const kDeepRecursionHex;
extern std::string const kInfiniteLoopWasmHex;
extern std::string const kStartLoopHex;

extern std::string const kBadAlignWasmHex;

extern std::string const kThousandParamsHex;
extern std::string const kThousand1ParamsHex;
extern std::string const kLocals10kHex;
extern std::string const kFunctions5kHex;

extern std::string const kOpcReservedHex;

extern std::string const kImpExpHex;
extern std::string const kUpdateDataWasmHex;
