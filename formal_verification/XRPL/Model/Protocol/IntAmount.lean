namespace XRPL.Model.Protocol

/-- An integer amount, such as `XRPAmount` and its count of drops. -/
structure IntAmount where
  value : Int64

/-- Mirrors `XRPAmount::signum`. -/
def IntAmount.signum (x : IntAmount) : Int :=
  if x.value < 0 then -1
  else if x.value != 0 then 1
  else 0

end XRPL.Model.Protocol
