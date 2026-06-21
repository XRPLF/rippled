import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.STAmount.Add.Common.IOU
import XRPL.Properties.Protocol.STAmount.Add.Common.DirectedSupport
import XRPL.Properties.Protocol.STAmount.Common.RoundToScaleHelpers
import XRPL.Properties.Protocol.IOUAmount.Common.Defs

/-! # Proof bodies for the `IOUAmount` addition correctness headlines.

`IOUAmount` addition double-rounds (the 19-digit `Number` add then the 16-digit canonical
snap), so it carries error bounds: a tight half-ULP relative bound under `to_nearest`, and
a full-ULP `RoundsWithin` bound for the directed modes. These reuse the proven IOU bounds
`IOUAmount.operator_add_rounds_to_nearest` / `operator_add_rounds_directed` from the STAmount
tree. The thin headlines live in `IOUAmount.Add.RoundsWithin`. -/

namespace XRPL.Model.Protocol

/-- Exponent slack: `InRange16` discharges the `operator_add` exponent hypotheses. -/
private lemma IOUAmount.add_exp_lo {e : Int} (h : (-96 : ℤ) ≤ e) : minExponent + 3 ≤ e := by
  have hm : minExponent = -32768 := rfl; omega

private lemma IOUAmount.add_exp_hi {e : Int} (h : e ≤ (80 : ℤ)) : e ≤ maxExponent - 4 := by
  have hM : maxExponent = 32768 := rfl; omega

/-- **`operator_add` rounds within the IOU ULP bound (every mode).** -/
theorem IOUAmount.operator_add_rounds_proof (x y result : IOUAmount) (mode : rounding_mode)
    (hx : x.InRange16) (hy : y.InRange16)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat + y.toRat) mode IOUAmount.εDirected :=
  IOUAmount.operator_add_rounds_directed x y result mode hx.mant_lo hx.mant_hi hy.mant_lo hy.mant_hi
    (IOUAmount.add_exp_lo hx.exp_lo) (IOUAmount.add_exp_hi hx.exp_hi)
    (IOUAmount.add_exp_lo hy.exp_lo) (IOUAmount.add_exp_hi hy.exp_hi)
    h_truth_ne hok hresult

/-- **`operator_add` rounds within a half-ULP (relative) under `to_nearest`.** -/
theorem IOUAmount.operator_add_rounds_to_nearest_proof (x y result : IOUAmount)
    (hx : x.InRange16) (hy : y.InRange16)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| ≤ |x.toRat + y.toRat| * IOUAmount.εToNearest :=
  IOUAmount.operator_add_rounds_to_nearest x y result hx.mant_lo hx.mant_hi hy.mant_lo hy.mant_hi
    (IOUAmount.add_exp_lo hx.exp_lo) (IOUAmount.add_exp_hi hx.exp_hi)
    (IOUAmount.add_exp_lo hy.exp_lo) (IOUAmount.add_exp_hi hy.exp_hi)
    h_truth_ne hok hresult

end XRPL.Model.Protocol
