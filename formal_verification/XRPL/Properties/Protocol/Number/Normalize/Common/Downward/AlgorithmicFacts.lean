import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.AlgorithmicFacts

namespace XRPL.Model.Protocol

theorem normalize_algorithmic_facts_downward (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRepUp.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      (maxRep.toNat < zm.toNat → f = 0 ∧ g.empty = true) ∧
      |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze ∧
      g.doRoundUp false zm ze largeRange.min largeRange.max .downward "Number::normalize 2" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = n.negative_ ∧
      g.sbit_ = n.negative_ ∧
      mantissaFloorSucc ≤ zm.toNat :=
  normalize_algorithmic_facts_anyMode n result .downward hn_mant_ne hok hresult

end XRPL.Model.Protocol
