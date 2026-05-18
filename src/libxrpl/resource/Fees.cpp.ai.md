# `src/libxrpl/resource/Fees.cpp`

## Role in the System

This file is the authoritative definition of every quantified resource charge in the XRPL node's rate-limiting and abuse-prevention system. It serves as a single, auditable table of numeric weights that the rest of the codebase uses when deciding how much to penalize a connected client or peer for particular categories of bad, expensive, or abusive behavior. Nothing about anti-abuse policy is scattered across individual call sites — it is concentrated here.

## The `Charge` Type

Each entry is a `const Charge` object, which pairs an integer `value_type` cost with a human-readable label string. `Charge` objects are immutable after construction — there is no mutation API — and comparisons are made purely on numeric cost. The label exists solely for logging and diagnostics. The `operator*` on `Charge` allows callers to scale a base fee (e.g., for repeated offenses) without defining separate constants.

## The Fee Schedule

The constants fall into four conceptual tiers reflecting the origin and severity of the request:

**General request problems** cover behavior detectable before heavy processing:
- `feeMalformedRequest` (200) — structurally invalid input the server can immediately reject.
- `feeRequestNoReply` (10) — a valid but unsatisfiable request (low penalty since this may be a benign race condition).
- `feeInvalidSignature` (2000) — cryptographic signature verification that failed; expensive to check, clearly abusive at scale.
- `feeUselessData` (150) — data received but not applicable.
- `feeInvalidData` (400) — data that required verification work before rejection.

**RPC loads** reflect cost to the server's JSON-RPC interface:
- `feeReferenceRPC` (20) is the baseline for a cheap, well-formed call.
- `feeMalformedRPC` (100) and `feeExceptionRPC` (100) penalize ill-formed or crashing calls.
- `feeMediumBurdenRPC` (400) and `feeHeavyBurdenRPC` (3000) penalize queries that drive significant computation — ledger traversals, path-finding, etc.

**Peer loads** cover activity on the overlay network between validators and relay nodes:
- `feeTrivialPeer` (1) for no-reply messages.
- `feeModerateBurdenPeer` (250) for messages requiring some local work.
- `feeHeavyBurdenPeer` (2000) for computationally intensive peer messages.

**Administrative / enforcement** constants mark the system's own enforcement actions:
- `feeWarning` (4000) is charged when the system sends a warning to a consumer already near the limit — the act of warning is itself priced to keep the accumulator rising.
- `feeDrop` (6000) is charged at the moment of disconnection, which deliberately pushes the balance high enough that a reconnecting peer is still penalized for a time after it reconnects.

## Integration with `Logic::charge`

These constants flow into `Resource::Logic::charge()`, the only place where a fee is actually applied to a tracked endpoint. That method uses three internal log-level cutoffs (`feeLogAsDebug = 100`, `feeLogAsInfo = 1000`, `feeLogAsWarn = 3000`) to decide how loudly to log each charge — a design explicitly tied to the numeric scale established here. The comment in `Fees.cpp` ("See also Resource::Logic::charge for log level cutoff values") documents this intentional coupling: anyone who adjusts a fee value needs to consider whether it crosses a logging threshold.

After applying the charge, `Logic::disposition()` compares the running balance against two thresholds: `warningThreshold` triggers a `Disposition::warn` and `dropThreshold` triggers `Disposition::drop`. The numeric spacing between fee values and those thresholds determines how many of each request type a client can issue before being warned or dropped. `feeInvalidSignature` (2000) alone can push a client toward the warning level in a single call, while `feeRequestNoReply` (10) would require hundreds. This calibration is the core policy decision embodied by this file.

## Design Rationale

Defining all charges as `const` globals in a single translation unit rather than as local magic numbers at each call site achieves two things. First, it makes the policy visible and comparable in one place — a reviewer can immediately see that a dropped connection (6000) costs three times a heavy RPC (3000) and thirty times a reference RPC (20). Second, it prevents inconsistent re-definitions: any code that needs to charge for an invalid signature imports the same `feeInvalidSignature` and cannot accidentally use a different value.

The absence of an `enum` or strongly-typed tag for fee categories is deliberate: `Charge` is an open value type, not a closed enumeration, so callers can scale (`fee * n`), log, and compare charges uniformly without needing to enumerate every case in a switch.