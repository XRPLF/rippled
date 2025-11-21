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
extern std::string const memoryLastByteOf8MBHex;
extern std::string const memoryGrow1MoreThan8MBHex;

extern std::string const table64ElementsHex;
extern std::string const table65ElementsHex;
extern std::string const table2TablesHex;

extern std::string const proposalMutableGlobalHex;
extern std::string const divideBy0Hex;
