import XRPL.Model.Protocol.STAmount

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

structure Rate where
  value : UInt32
  deriving DecidableEq, Repr, BEq

def kParityRate : Rate := ⟨1000000000⟩

-- C++ detail::asAmount(rate): STAmount{noIssue, rate.value, -9}
private def Rate.asAmount (rate : Rate) (mode : rounding_mode) : Except String STAmount :=
  STAmount.ofInt64 (.issue noIssue) rate.value.toUInt64.toInt64 (-9) mode

-- C++ multiply(STAmount, Rate); renamed: STAmount.multiply is the two-amount multiply
def multiplyRate (amount : STAmount) (rate : Rate) (mode : rounding_mode)
    : Except String STAmount :=
  if rate == kParityRate then
    .ok amount
  else
    match rate.asAmount mode with
    | .error e => .error e
    | .ok rateAmt => STAmount.multiply amount rateAmt amount.asset mode

end XRPL.Model.Protocol
