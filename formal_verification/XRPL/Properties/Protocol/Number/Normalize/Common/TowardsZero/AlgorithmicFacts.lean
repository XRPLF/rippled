import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.AlgorithmicFacts

namespace XRPL.Model.Protocol

theorem normalize_algorithmic_facts_towards_zero (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRepUp.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (maxRep.toNat < zm.toNat → f = 0 ∧ g.empty = true) ∧
      |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze ∧
      g.doRoundUp false zm ze largeRange.min largeRange.max .towards_zero "Number::normalize 2" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = n.negative_ ∧
      mantissaFloorSucc ≤ zm.toNat := by
  obtain ⟨zm, ze, f, g, res_pos, hc1, hc2, hc3, hc4, _hc5, hc6, hc7, hc8, hc9, hc10, hc11, hc12, _hc13, hc14⟩ :=
    normalize_algorithmic_facts_anyMode n result .towards_zero hn_mant_ne hok hresult
  exact ⟨zm, ze, f, g, res_pos, hc1, hc2, hc3, hc4, hc6, hc7, hc8, hc9, hc10, hc11, hc12, hc14⟩

end XRPL.Model.Protocol
