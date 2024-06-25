//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_TX_XCHAINBRIDGE_H_INCLUDED
#define RIPPLE_TX_XCHAINBRIDGE_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/XChainAttestations.h>

namespace ripple {

constexpr size_t xbridgeMaxAccountCreateClaims = 128;

// Attach a new bridge to a door account. Once this is done, the cross-chain
// transfer transactions may be used to transfer funds from this account.
class XChainCreateBridge : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit XChainCreateBridge(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

class BridgeModify : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit BridgeModify(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};
//------------------------------------------------------------------------------

// Claim funds from a `XChainCommit` transaction. This is normally not needed,
// but may be used to handle transaction failures or if the destination account
// was not specified in the `XChainCommit` transaction. It may only be used
// after a quorum of signatures have been sent from the witness servers.
//
// If the transaction succeeds in moving funds, the referenced `XChainClaimID`
// ledger object will be destroyed. This prevents transaction replay. If the
// transaction fails, the `XChainClaimID` will not be destroyed and the
// transaction may be re-run with different parameters.
class XChainClaim : public Transactor
{
public:
    // Blocker since we cannot accurately calculate the consequences
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit XChainClaim(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

// Put assets into trust on the locking-chain so they may be wrapped on the
// issuing-chain, or return wrapped assets on the issuing-chain so they can be
// unlocked on the locking-chain. The second step in a cross-chain transfer.
class XChainCommit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    explicit XChainCommit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

// Create a new claim id owned by the account. This is the first step in a
// cross-chain transfer. The claim id must be created on the destination chain
// before the `XChainCommit` transaction (which must reference this number) can
// be sent on the source chain. The account that will send the `XChainCommit` on
// the source chain must be specified in this transaction (see note on the
// `SourceAccount` field in the `XChainClaimID` ledger object for
// justification). The actual sequence number must be retrieved from a validated
// ledger.
class XChainCreateClaimID : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit XChainCreateClaimID(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

// Provide attestations from a witness server attesting to events on
// the other chain. The signatures must be from one of the keys on the door's
// signer's list at the time the signature was provided. However, if the
// signature list changes between the time the signature was submitted and the
// quorum is reached, the new signature set is used and some of the currently
// collected signatures may be removed. Also note the reward is only sent to
// accounts that have keys on the current list.
class XChainAddClaimAttestation : public Transactor
{
public:
    // Blocker since we cannot accurately calculate the consequences
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit XChainAddClaimAttestation(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

class XChainAddAccountCreateAttestation : public Transactor
{
public:
    // Blocker since we cannot accurately calculate the consequences
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit XChainAddAccountCreateAttestation(ApplyContext& ctx)
        : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

// This is a special transaction used for creating accounts through a
// cross-chain transfer. A normal cross-chain transfer requires a "chain claim
// id" (which requires an existing account on the destination chain). One
// purpose of the "chain claim id" is to prevent transaction replay. For this
// transaction, we use a different mechanism: the accounts must be claimed on
// the destination chain in the same order that the `XChainCreateAccountCommit`
// transactions occurred on the source chain.
//
// This transaction can only be used for XRP to XRP bridges.
//
// IMPORTANT: This transaction should only be enabled if the witness
// attestations will be reliably delivered to the destination chain. If the
// signatures are not delivered (for example, the chain relies on user wallets
// to collect signatures) then account creation would be blocked for all
// transactions that happened after the one waiting on attestations. This could
// be used maliciously. To disable this transaction on XRP to XRP bridges, the
// bridge's `MinAccountCreateAmount` should not be present.
//
// Note: If this account already exists, the XRP is transferred to the existing
// account. However, note that unlike the `XChainCommit` transaction, there is
// no error handling mechanism. If the claim transaction fails, there is no
// mechanism for refunds. The funds are permanently lost. This transaction
// should still only be used for account creation.
class XChainCreateAccountCommit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit XChainCreateAccountCommit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

}  // namespace ripple

#endif
