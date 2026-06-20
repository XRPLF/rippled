import XRPL.Properties.Protocol.Number.Add.Common.Decompose
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp


namespace XRPL.Model.Protocol

/-- For diff-sign Numbers, `|x.toRat + y.toRat| = abs (|x.toRat| - |y.toRat|)`. -/
lemma abs_add_eq_of_diff_sign {x y : Number} (h : x.negative_ ≠ y.negative_) :
    |x.toRat + y.toRat| = |(|x.toRat| - |y.toRat|)| := by
  cases hxn : x.negative_
  · have hyn : y.negative_ = true := by
      cases hh : y.negative_
      · rw [hxn, hh] at h; exact absurd rfl h
      · rfl
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
    rw [abs_of_nonneg hx_nn, abs_of_nonpos hy_np]
    -- x + y = x - (-y), since y ≤ 0 so -y ≥ 0
    have h_eq : x.toRat + y.toRat = x.toRat - (-y.toRat) := by ring
    rw [h_eq]
  · have hyn : y.negative_ = false := by
      cases hh : y.negative_
      · rfl
      · rw [hxn, hh] at h; exact absurd rfl h
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
    rw [abs_of_nonpos hx_np, abs_of_nonneg hy_nn]
    -- x + y = y - (-x); we want | y - x | but get | -x - y |
    have h_eq : x.toRat + y.toRat = -(-x.toRat - y.toRat) := by ring
    rw [h_eq, abs_neg]

end XRPL.Model.Protocol
