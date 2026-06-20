import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.SameSignDecompose

namespace XRPL.Model.Protocol

theorem operator_add_algorithmic_facts_same_sign_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      PostAlignSpec (x.toRat + y.toRat) x.negative_ result .upward zm ze' f g res_pos :=
  operator_add_algorithmic_facts_same_sign_anyMode x y result .upward
    hx hy hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok

end XRPL.Model.Protocol
