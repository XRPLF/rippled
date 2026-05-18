# `PaymentChannelCreate.cpp`

## Role in the System

This file implements the `PaymentChannelCreate` transactor — the on-ledger entry point for opening an XRP payment channel. Payment channels are XRPL's primary primitive for streaming micropayments: the channel owner locks XRP into a dedicated ledger object, then issues cryptographically-signed claims off-ledger. The recipient can present a claim at any time to settle on-chain, while the owner can continuously authorize higher amounts without touching the ledger. Only opening the channel, adding funds (`PaymentChannelFund`), claiming (`PaymentChannelClaim`), and closing require ledger transactions — the high-frequency payment flow happens entirely off-chain.

`PaymentChannelCreate.cpp` is responsible only for the construction phase. It sits alongside `PaymentChannelClaim.cpp` and `PaymentChannelFund.cpp` in the same directory, forming the complete lifecycle of a payment channel.

## Three-Phase Transaction Model

Like all transactors, `PaymentChannelCreate` is split across four methods following XRPL's layered validation architecture.

**`makeTxConsequences`** declares the worst-case XRP consumed by this transaction — `sfAmount` plus the transaction fee. This is used by the network before applying the transaction to determine whether a sender can afford it.

**`preflight`** runs with no ledger access and performs purely structural checks:
- `sfAmount` must be XRP (not an IOU) and strictly positive. Using `isXRP()` and comparing against `beast::zero` rejects both wrong-currency amounts and zero-value channels that would have no purpose.
- The source account cannot be its own destination (`temDST_IS_SRC`). Allowing this would create a nonsensical self-paying channel that serves no off-ledger settlement purpose.
- `sfPublicKey` must pass `publicKeyType()` — only known cryptographic key formats (secp256k1 or Ed25519) are accepted. The key is what the owner will later use to sign off-ledger claims; an unrecognized format would make every claim unverifiable.

**`preclaim`** runs against a read-only snapshot of the current ledger and validates state-dependent conditions:
- The source account must exist.
- Two separate reserve checks ensure the account can both cover the incremental reserve for owning one more ledger object (`balance < reserve`) and additionally fund the channel itself (`balance < reserve + amount`). The two-step distinction matters: `tecINSUFFICIENT_RESERVE` means the account cannot afford the reserve at all, while `tecUNFUNDED` means it could afford the reserve but not the requested channel amount.
- The destination must exist (`tecNO_DST`). There is a deliberate design choice not to create channels to non-existent accounts, even though XRP would be held in escrow — this avoids reserving funds to an address that may never be activated.
- If the destination has set `lsfDisallowIncomingPayChan`, the creation is rejected with `tecNO_PERMISSION`. This lets accounts opt out of receiving unsolicited channels.
- If the destination has set `lsfRequireDestTag`, a `sfDestinationTag` must be present — otherwise `tecDST_TAG_NEEDED` is returned.
- `isPseudoAccount(sled)` prevents channels from being directed at pseudo-accounts (synthetic account objects that back ledger features rather than real users). This check is intentionally not amendment-gated: because pseudo-account discriminator fields are themselves only written under amendment guards, the check naturally behaves correctly across all amendment states.

**`doApply`** mutates the ledger, assuming all validations passed. It performs two amendment-gated checks and then constructs the channel SLE.

## Key Design Decisions

**Channel key derivation.** `keylet::payChan(account, dst, ctx_.tx.getSeqValue())` hashes the source account, destination account, and transaction sequence (or ticket) number into the channel's ledger key. This ensures that two channels between the same pair of accounts are always addressable distinctly. Using `getSeqValue()` rather than `getSeq()` transparently handles both regular sequence numbers and ticket-based transactions — a detail acknowledged by a code comment referencing `SeqProxy.h`.

**`sfCancelAfter` expiry check in `doApply`, not `preclaim`.** The `fixPayChanCancelAfter` amendment adds a guard that rejects channel creation if the optional `sfCancelAfter` timestamp is already past the ledger's `parentCloseTime`. This check lives in `doApply` rather than `preclaim` because the canonical close time of the ledger under construction is only fully determined at apply time. Running it during `preclaim` against an earlier view could allow a transaction through that immediately expires.

**`sfBalance` initialized via `zeroed()`.** The newly created SLE sets `sfBalance` to `ctx_.tx[sfAmount].zeroed()` — not a literal zero, but a zero of the same `XRPAmount` type. This makes the channel's "amount paid so far" field type-consistent with its "total funded" field from the start.

**`fixIncludeKeyletFields` amendment.** When active, `sfSequence` is written directly into the channel SLE. This allows off-ledger clients and tools to reconstruct the channel's `keylet` purely from the object itself, without needing the originating transaction data. This was a bug-fix amendment: channels created before it lack the sequence field, requiring callers to retrieve the transaction to compute the key.

**Dual directory insertion.** The channel is inserted into both the owner's directory and the destination's directory, with the resulting page numbers stored as `sfOwnerNode` and `sfDestinationNode` respectively. This is the standard XRPL pattern for objects that two parties have claims to: both accounts can enumerate the channel through their own account's linked object list, and removal is efficiently reversible during claim/close operations.

**Owner count and balance mutation.** At the end of `doApply`, the source account's `sfBalance` is decremented by `sfAmount` (the XRP moves into the channel SLE), and `adjustOwnerCount` increments the owner count by one. The owner count increase raises the account's required reserve, reinforcing that the channel occupies a real slot in the ledger's object hierarchy.

## Relationship to Sibling Transactors

`PaymentChannelFund.cpp` adds XRP to an existing channel: it updates `sfAmount` on the channel SLE directly and debits the owner, without creating any new SLE or touching directories. `PaymentChannelClaim.cpp` handles the claim flow — verifying the off-ledger signature, updating `sfBalance`, optionally closing the channel, and returning any remaining funds to the owner. `PaymentChannelCreate` is the only transactor that allocates the SLE and both directory entries; all others assume those structures already exist.