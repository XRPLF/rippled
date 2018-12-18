//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/LoadManifest.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

std::uint64_t
LoadManifest::calculateBaseFee (
    ReadView const& view,
    STTx const& tx)
{
    // Storing a new object, requires a fee that is enough to offset the cost
    // imposed on everyone; charge exactly 1 account reserve.
    if (tx.getFlags() & tfPayReserve)
        return view.fees().reserve * view.fees().units / view.fees().base;

    // Updating an existing record has the same cost as any other transaction.
    return Transactor::calculateBaseFee(view, tx);
}

NotTEC
LoadManifest::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureOnLedgerManifests))
        return temUNKNOWN;

    if (ctx.tx.getFlags() & tfLoadManifestMask)
        return temINVALID_FLAG;

    auto const ret = preflight1 (ctx);

    if (!isTesSuccess (ret))
        return ret;

    auto const mb = ctx.tx[sfManifest];

    if (mb.size() < 32 || mb.size() > 768)
        return temMANIFEST_MALFORMED;

    return preflight2 (ctx);
}

TER
LoadManifest::preclaim(PreclaimContext const& ctx)
{
    if (! ctx.view.rules().enabled(featureOnLedgerManifests))
        return temDISABLED;

    auto const m = deserializeManifest(ctx.tx[sfManifest]);

    if (!m)
        return tecMANIFEST_MALFORMED;

    // Existing code will not deserialize a manifest with a domain name
    // that is longer than maxDomainLength. However, such a change, if
    // ever made, would be transaction breaking if deployed without an
    // amendment. This extra check here protects from this unlikely
    // scenario.
    if (m->domain.size() > maxDomainLength)
        return tecMANIFEST_MALFORMED;

    auto const sle = ctx.view.read(keylet::manifest(m->masterKey));

    // The manifest we're trying to update must already exist
    if (!sle && !(ctx.tx.getFlags() & tfPayReserve))
        return tecNO_ENTRY;

    // The manifest must have a sequence number greater than any existing one
    if (sle && (*sle)[sfSequence] >= m->sequence)
        return tecMANIFEST_BAD_SEQUENCE;

    if (!m->verify())
        return tecMANIFEST_BAD_SIGNATURE;

    return tesSUCCESS;
}

TER
LoadManifest::doApply()
{
    if (! ctx_.view().rules().enabled(featureOnLedgerManifests))
        return temDISABLED;

    auto m = deserializeManifest(ctx_.tx[sfManifest]);
    assert(m);

    auto const key = keylet::manifest(m->masterKey);

    auto sle = view().peek(key);
    bool const found = static_cast<bool>(sle);

    if (!found)
    {
        sle = std::make_shared<SLE>(key);
        (*sle)[sfPublicKey] = m->masterKey;
    }

    assert (m->sequence >= (*sle)[sfSequence]);

    boost::optional<Slice> domain;

    if (!m->domain.empty())
        domain.emplace (makeSlice(m->domain));

    (*sle)[sfSequence] = m->sequence;
    (*sle)[sfManifest] = ctx_.tx[sfManifest];

    if ((*sle)[~sfDomain] != domain)
        (*sle)[~sfDomain] = domain;

    if (found)
        view().update(sle);
    else
        view().insert(sle);

    return tesSUCCESS;
}

}
