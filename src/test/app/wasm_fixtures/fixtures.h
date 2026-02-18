#pragma once

// TODO: consider moving these to separate files (and figure out the build)

#include <string>

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
extern std::string const badAlignWasmHex;

extern std::string const thousandParamsHex;
extern std::string const thousand1ParamsHex;
extern std::string const locals10kHex;
extern std::string const functions5kHex;

extern std::string const opcReservedHex;
