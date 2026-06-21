import Mathlib.Tactic


namespace XRPL.Model.Protocol

abbrev LedgerIndex := UInt32

-- chrono.h: uint32 seconds since 2000/01/01
abbrev NetClock.TimePoint := UInt32

-- std::numeric_limits<uintN>::max()
def maxUInt32 : UInt32 := 0xFFFFFFFF
def maxUInt64 : UInt64 := 0xFFFFFFFFFFFFFFFF

-- max recursion depth for vault-share asset checks (frozen / requireAuth)
def kMaxAssetCheckDepth : Nat := 5

end XRPL.Model.Protocol
