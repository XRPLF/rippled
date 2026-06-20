import Mathlib.Tactic
import XRPL.Model.Protocol.Number

open XRPL.Model.Protocol

macro "norm_isNormalized" : tactic =>
  `(tactic| (right; refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide))

macro "except_clash" eq:ident h_ok:ident : tactic =>
  `(tactic| (rw [$eq:ident] at $h_ok:ident; exact absurd $h_ok (by intro h; cases h)))

macro "underflow_absurd" h_ne:ident h_under:ident h_ok:ident : tactic =>
  `(tactic|
    (exfalso;
     apply $h_ne;
     simp only [if_pos $h_under] at $h_ok:ident;
     have hzexp : ¬ ((-2147483648 : Int) > maxExponent) := (by norm_num [maxExponent]);
     simp only [hzexp, if_false] at $h_ok:ident;
     exact (Except.ok.inj $h_ok).symm ▸ rfl))
