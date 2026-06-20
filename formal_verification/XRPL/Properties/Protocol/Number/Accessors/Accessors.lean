import XRPL.Properties.Protocol.Number.Accessors.Common.Proofs


namespace XRPL.Model.Protocol

/-- The public mantissa() and exponent() accessors are faithful:
  `mantissa · 10^exponent = toRat`. -/
theorem mantissa_mul_exponent_eq_toRat (n : Number) (hn : n.isNormalized) :
    (n.mantissa.toInt : ℚ) * (10 : ℚ) ^ n.exponent = n.toRat :=
  mantissa_mul_exponent_eq_toRat_proof n hn

/-- The public mantissa fits the signed rep: `|mantissa| ≤ maxRep = 2^63 - 1` -/
theorem mantissa_natAbs_le_maxRep (n : Number) (_hn : n.isNormalized) :
    (n.mantissa).toInt.natAbs ≤ maxRep.toNat :=
  mantissa_natAbs_le_maxRep_proof n _hn

end XRPL.Model.Protocol
