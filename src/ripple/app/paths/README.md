Theory of Pathfinding
=====================

It's a hard problem - an exponential problem - and there is a limited amount of time until the ledger changes.

Search for the best path, but it's impossible to guarantee the result is the best unless the ledger is small.

Also, the current state of the ledger can't be definitively known. We only know the state of past ledgers.

By the time a transaction is processed, liquidity may be taken or added. This is why payments support a maximum cost.

People are given estimates so they can make a decision as to whether the overall cost is good enough for them.

This is the **rippled** implementation; there are many other possible implementations!

**rippled** uses a variety of search techniques:

1. Hebbian learning.
 * Reuse found liquidity.
 * Good liquidity is often reusable.
 * When searching, limit our search to the rippled cache.
2. Six degrees of separation.
 * If sending value through individual's account, expect no path to have more than six hops.
 * According to [Facebook studies](https://www.facebook.com/notes/facebook-data-team/anatomy-of-facebook/10150388519243859) as of late 2011, its users are separated by fewer than five steps.
 * By using XRP for bridging the most complicated path expected is usually:
 * source -> gateway -> XRP -> gateway -> destination
3. Pareto principle.
 * Good liquidity is often reusable.
 * Concentrate on the most liquid gateways to serve almost everybody.
 * People who chose to use illiquid gateways get the service level implied by their choice of poor liquidity.
4. Monte Carlo methods.
 * Learn new paths.
 * Search outside the **rippled** cache.
 * Distributed learning.
 * Every rippled node searches their own cache for best liquidity and then the good results get propagated to the network.
 * Whenever somebody makes a payment, the nodes involved go to the rippled cache; since payments appear in every ledger in the network, this liquidity information now appears in every rippled cache.


Life of a Payment
======================

Overview
----------

Making a payment in the ripple network is about finding the cheapest path between source and destination.

There are various stages:

An issue is a balance in some specific currency.  An issuer is someone who "creates" a currency by creating an issue.

For tx processing, people submit a tx to a rippled node, which attempts to apply the tx locally first, and if succeesful, distributes it to other nodes.

When someone accepts payment they list their specific payment terms, "what must happen before the payment goes off."  That can be done completely in the Ripple Network.  Normally a payment on the Ripple net can be completely settled there.  When the ledger closes, the terms are met, or they are not met.

For a bridge payment, based on a previous promise it will deliver payment off-network. A bridge promises to execute the final leg of a payment, but there's the possibility of default. If you want to trust a bridge for a specific pathfinding request, you need to include it in the request.

In contrast, a gateway is in the business of redeeming value on the Ripple Network for value off the Ripnet.  A gateway is in the business of issuing balances.

Bitstamp is a gateway - you send 'em cash, they give you a ripple balance and vice versa.  There's no promise or forwarding for a transaction.

A bridge is a facility that allows payments to be made from the Ripnet to off the Ripnet.

Suppose I'm on the Ripple network and want to send value to the bitcoin network.  See:  https://ripple.com/wiki/Outbound_Bridge_Payments
https://ripple.com/wiki/Services_API


Two types of paths:
1. I have X amount of cash I wish to send to someone - how much will they receive?
 * not yet implemented in pathfinding, or at least in the RPC API.
   * TODO: find if this is implemented and either document it or file a JIRA.
2. I need to deliver X amount of cash to someone - how much will it cost me?

Here's a transaction:
https://ripple.com/wiki/Transaction_Format#Payment_.280.29

Not implemented: bridge types.



High level of a payment
-----------------------

1. I make a request for path finding to a rippled.
2. That rippled "continuously" returns solutions until I "stop".
3. You create a transaction which includes the path set that you got from the quote.
4. You sign it.
5. You submit it to one or more rippleds.
6. Those rippled validates it, and forwards it if it;s valid.
 * valid means "not malformed" and "can claim a fee" - the sending account has enough to cover the transaction fee.
7. At ledger closing time, the transaction is applied by the transaction engine and the result is stored by changing the ledger and storing the tx metadata in that ledger.

If you're sending and receiving XRP you don't need a path.
If the union of the default paths are sufficient, you don't need a path set (see below about default paths).
The idea behind path sets is to provide sufficient liquidity in the face of a changing ledger.

If you can compute a path already yourself or know one, you don't need to do steps 1 or 2.


1. Finding liquidity - "path finding"
  * finding paths (exactly "path finding")
  * filtering by liquidity.
    * take a selection of paths that should satisfy the transaction's conditions.
    * This routine is called RippleCalc::rippleCalc.

2. Build a payment transaction containing the path set.
  * a path set is a group of paths that a transaction is supposed to use for liquidity
  * default paths are not included
    * zero hop path: a default path is "source directly to destination".
    * one hop paths - because they can be derived from the source and destination currency.
  * A payment transaction includes the following fields
    * the source account
    * the destination account
    * destination amount
      * contains an amount and a currency
      * if currency not XRP, must have a specified issuer.
      * If you specify an issuer then you MUST deliver that issuance to the destination.
      * If you specify the issuer as the destination account, then they will receive any of the issuances they trust.
    * maximum source amount - maximum amount to debit the sender.
      * This field is optional.
      * if not specified, defaults to the destination amount with the sender as issuer.
      * contains an amount and a currency
      * if currency not XRP, must have a specified issuer.
      * specifying the sender as issuer allows any of the sender's issuance to be spent.
      * specifying a specific issuer allows only that specific issuance to be sent.
  * path set.
    * Optional.
    * Might contain "invalid" paths or even "gibberish" for an untrusted server.
    * An untrusted server could provide proof that their paths are actually valid.
      * That would NOT prove that this is the cheapest path.
    * The client needs to talk to multiple untrusted servers, get proofs and then pick the best path.
    * It's much easier to validate a proof of a path than to find one, because you need a lot of information to find one.
       * In the future we might allow one server to validate a path offered by another server.

3. Executing a payment
 * very little time, can't afford to do a search.
 * that's why we do the path building before the payment is due.
 * The routine used to compute liquidity and ledger change is also called RippleCalc::rippleCalc.
