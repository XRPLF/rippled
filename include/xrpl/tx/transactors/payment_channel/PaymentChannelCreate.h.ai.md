# `PaymentChannelCreate.h` — Payment Channel Creation Transactor

`PaymentChannelCreate` is the transactor responsible for opening a unidirectional XRP payment channel on the XRPL. It lives in the `payment_channel` transactor family alongside `PaymentChannelFund` and `PaymentChannelClaim`, and inherits from `Transactor` — the common base class that orchestrates the three-phase transaction pipeline.

## Role in the System

Payment channels allow two parties to exchange a stream of XRP payments off-ledger, checkpointed by signed claim messages, without committing each individual transfer to the global ledger. Only the open and close operations are on-ledger; individual micropayments travel off-chain. `PaymentChannelCreate` handles the first on-ledger step: locking the sender's XRP into a dedicated channel ledger object (`PayChannel` SLE) and notifying both sender and recipient by registering the channel in each party's owner directory.

## Class Interface and Inheritance

The class derives from `Transactor` and exposes the standard four-point contract:

- `makeTxConsequences` — static, called before the ledger is read
- `preflight` — static, pure field-level validation
- `preclaim` — static, read-only ledger checks
- `doApply` — instance method, mutates the ledger

The `ConsequencesFactory` tag is set to `Custom`, which is the telling design decision: unlike `PaymentChannelClaim` (tagged `Normal`), creating a channel locks up the full `sfAmount` of XRP beyond just the fee. `makeTxConsequences` therefore constructs a `TxConsequences` object that reports `ctx.tx[sfAmount].xrp()` as the consumed balance, not merely the transaction fee. This lets the transaction queue accurately represent how much XRP the account will be unable to use while this transaction is pending.

## Preflight Validation

`preflight` enforces three invariants before any ledger access occurs:

1. The `sfAmount` field must be a positive XRP value — IOUs are not permitted in payment channels.
2. The sender (`sfAccount`) must differ from the destination (`sfDestination`); self-funded channels are nonsensical and rejected with `temDST_IS_SRC`.
3. The `sfPublicKey` must parse as a recognized key type. This key is the one the sender will use to sign off-ledger claims, so its validity must be established at creation time — it cannot be changed later.

## Preclaim Checks

`preclaim` reads the ledger without modifying it and enforces economic and permission constraints:

- **Reserve and funding**: The sender must have enough balance to cover both the post-creation owner reserve (`ownerCount + 1`) and the full channel amount. The reserve check uses `tecINSUFFICIENT_RESERVE`, while the funding check returns `tecUNFUNDED`, preserving the distinction between "account is too lean to hold more objects" and "account can't actually transfer the requested amount."
- **Destination existence**: The destination account must already exist on the ledger (`tecNO_DST`).
- **Incoming channel permission**: If the destination account has set `lsfDisallowIncomingPayChan`, the creation is refused with `tecNO_PERMISSION`.
- **Destination tag requirement**: If the destination requires a tag (`lsfRequireDestTag`) and none was provided in the transaction, `tecDST_TAG_NEEDED` is returned.
- **Pseudo-account guard**: Pseudo-accounts (fee sink accounts, amendment accounts, etc.) cannot receive payment channels through this path. Notably, this check is *not* amendment-gated — the comment explains that writes to pseudo-account discriminator fields are themselves amendment-gated upstream, so the check's behavior automatically tracks the active amendments without needing its own gate.

## Ledger Mutation in `doApply`

The apply phase performs several coordinated mutations:

**Expiry validation (amendment-gated)**: Under `fixPayChanCancelAfter`, an optional `sfCancelAfter` field is checked against `parentCloseTime`. This happens in `doApply`, not `preclaim`, because the canonical close time of the current ledger is only fully settled at apply time.

**Channel SLE construction**: A new `PayChannel` ledger object is created at a keylet derived from `keylet::payChan(account, dst, ctx_.tx.getSeqValue())`. Using the transaction's sequence-or-ticket value as part of the key makes the channel ID fully deterministic before the transaction lands on the ledger — an important property for off-ledger parties who need to reference the channel in signed claims before the creating transaction has been confirmed. The SLE captures: total escrowed funds (`sfAmount`), the running paid balance initialized to zero (`sfBalance` via `zeroed()`), both account IDs, the settle delay, the signing public key, and the optional cancel-after, source tag, and destination tag fields. Under `fixIncludeKeyletFields`, the sequence value is also written directly into the SLE to allow key reconstruction from the object alone.

**Dual directory registration**: The new channel is inserted into the owner directory of *both* the sender and the recipient. The sender's entry enables reserve accounting and channel discovery for funding or closure; the recipient's entry gives them a direct path to claim funds. Each insertion records the directory page in `sfOwnerNode` and `sfDestinationNode` respectively, enabling efficient removal during channel closure.

**Balance and owner count update**: The sender's `sfBalance` is decremented by the full channel amount, and `adjustOwnerCount` increments the sender's `sfOwnerCount` by one, raising their minimum reserve to reflect the new ledger object.

## Relationship to Sibling Transactors

`PaymentChannelFund` shares the `Custom` consequences pattern with `PaymentChannelCreate` and uses the same `makeTxConsequences` signature, since adding funds to an existing channel also moves XRP. `PaymentChannelClaim`, by contrast, uses `Normal` consequences — a claim doesn't move XRP from the claimer's balance, only from the channel's escrowed pool. `PaymentChannelClaim` also overrides `checkExtraFeatures` and `getFlagsMask`, reflecting that it has its own feature-gate and supports additional transaction flags (e.g. closing or renewing the channel) that `PaymentChannelCreate` does not.