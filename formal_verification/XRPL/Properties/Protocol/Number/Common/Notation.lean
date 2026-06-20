/-- Meaningfully-named numeric-literal notations for the Number proofs.
These expand to bare polymorphic numerals (transparent to omega/linarith/
norm_num/decide at both ℕ and ℚ), so they work inside proofs where an
`abbrev`/`def` would not. Pure syntax — no imports needed. -/

notation "mantissaFloor" => 922337203685477580
notation "mantissaFloorSucc" => 922337203685477581

/-- `maxRep.toNat = 2^63 - 1` — the ℕ/ℚ literal form of `maxRep` (which as a
    `UInt64` `def` is opaque to `omega`). -/
notation "maxRepNat" => 9223372036854775807

/-- `maxRepUp.toNat = 2^63 + 2` — the ℕ/ℚ literal form of `maxRepUp` (the
    cusp round-up cap; numerically equals `maxRepCuspTarget`). Re-derived via
    `rfl` all over the rounding tree; naming it keeps those proofs readable. -/
notation "maxRepUpNat" => 9223372036854775810

/-- `2^63 + 2` — the maxRep-cusp round-up target (and the `10/(2^63+2)`
    same-sign directed bound denominator). -/
notation "maxRepCuspTarget" => 9223372036854775810

/-- `10^19 - 10` — the largest `%10=0` mantissa witness used in `Closest/`. -/
notation "maxMul10Witness" => 9999999999999999990

/-- The mantissa precision: `Int.log 10 largeRange.min = 18`. Used standalone in
    exponent arithmetic (NOT the `18` inside `10^18` or `2^63-18`). -/
notation "mantissaLog" => 18

/-- `10^19` (= `largeRange.max + 1`), the UInt128 scale factor in mul/div. -/
notation "tenPow19" => 10000000000000000000

/-- `2^64` — the `UInt64` modulus, in `(2:ℕ)^64 = … := by decide` lemmas. -/
notation "twoPow64" => 18446744073709551616

/-- `maxRep + 93` (= 2^63+92) — the mul-upward sharpness-witness mantissa. -/
notation "mulUpwardWitness" => 9223372036854775900

/-- `2^63` (= maxRep + 1) — the rounding cusp; normalize tightness witnesses are `2^63 ± k`. -/
notation "twoPow63" => 9223372036854775808
notation "twoPow63Add1"  => 9223372036854775809  -- 2^63+1 (towards_zero witness input)
notation "twoPow63Add7"  => 9223372036854775815  -- 2^63+7 (to_nearest witness input)
notation "twoPow63Add12" => 9223372036854775820  -- 2^63+12 (to_nearest witness result)
notation "twoPow63Sub8"  => 9223372036854775800  -- 2^63-8 = 10*mantissaFloor (truncate result)
