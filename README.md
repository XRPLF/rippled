![Ripple](/images/ripple.png)

#The World’s Fastest and Most Secure Payment System

**What is Ripple?**

Ripple is the open-source, distributed payment protocol that enables instant
payments with low fees, no chargebacks, and currency flexibility (for example
dollars, yen, euros, bitcoins, or even loyalty points). Businesses of any size
can easily build payment solutions such as banking or remittance apps, and
accelerate the movement of money. Ripple enables the world to move value the
way it moves information on the Internet.

![Ripple Network](images/network.png)

**What is a Gateway?**

Ripple works with gateways: independent businesses which hold customer
deposits in various currencies such as U.S. dollars (USD) or Euros (EUR),
in exchange for providing cryptographically-signed issuances that users can
send and trade with one another in seconds on the Ripple network. Within the
protocol, exchanges between multiple currencies can occur atomically without
any central authority to monitor them. Later, customers can withdraw their
Ripple balances from the gateways that created those issuances.

**How do Ripple payments work?**

A sender specifies the amount and currency the recipient should receive and
Ripple automatically converts the sender’s available currencies using the
distributed order books integrated into the Ripple protocol. Independent third
parties acting as  market makers provide liquidity in these order books.

Ripple uses a pathfinding algorithm that considers currency pairs when
converting from the source to the destination currency. This algorithm searches
for a series of currency swaps that gives the user the lowest cost. Since
anyone can participate as a market maker, market forces drive fees to the
lowest practical level.

**What can you do with Ripple?**

The protocol is entirely open-source and the network’s shared ledger is public
information, so no central authority prevents anyone from participating. Anyone
can become a market maker, create a wallet or a gateway, or monitor network
behavior. Competition drives down spreads and fees, making the network useful
to everyone.


###Key Protocol Features
1. XRP is Ripple’s native [cryptocurrency]
(http://en.wikipedia.org/wiki/Cryptocurrency) with a fixed supply that
decreases slowly over time, with no mining. XRP acts as a bridge currency, and
pays for transaction fees that protect the network against spam.
![XRP as a bridge currency](/images/vehicle_currency.png)

2. Pathfinding discovers cheap and efficient payment paths through multiple
[order books](https://www.ripplecharts.com) allowing anyone to trade anything.
When two accounts aren’t linked by relationships of trust, the Ripple pathfinding
engine considers intermediate links and order books to produce a set of possible
paths the transaction can take. When the payment is processed, the liquidity
along these paths is iteratively consumed in best-first order.
![Pathfinding from Dollars to Euro](/images/pathfinding.png)

3. [Consensus](https://www.youtube.com/watch?v=pj1QVb1vlC0) confirms
transactions in an atomic fashion, without mining, ensuring efficient use of
resources.

#rippled - Ripple P2P server

##[![Build Status](https://travis-ci.org/ripple/rippled.png?branch=develop)](https://travis-ci.org/ripple/rippled)

This is the repository for `rippled`, the reference implementation of the Ripple P2P server.

###Build instructions:
* https://ripple.com/wiki/Rippled_build_instructions

###Setup instructions:
* https://ripple.com/wiki/Rippled_setup_instructions

###Issues
* https://github.com/ripple/rippled/issues

### Repository Contents

#### ./bin
Scripts and data files for Ripple integrators.

#### ./build
Intermediate and final build outputs.

#### ./Builds
Platform or IDE-specific project files.

#### ./doc
Documentation and example configuration files.

#### ./src
Source code directory. Some of the directories contained here are
external repositories inlined via git-subtree, see the corresponding
README for more details.

#### ./test
Javascript / Mocha tests.

## License
`rippled` is open source and permissively licensed under the ISC license. See the
LICENSE file for more details.

###For more information:
* [Ripple Knowledge Center](https://ripple.com/learn/)
* [Ripple Developer Center](https://ripple.com/build/)
* [Ripple Solutions](https://ripple.com/files/ripple_solutions_guide.pdf)
* [Ripple Executive Summary for Financial Institutions](https://ripple.com/solutions/executive-summary-for-financial-institutions/)

- - -

Copyright © 2015, Ripple Labs. All rights reserved.

Portions of this document, including but not limited to the Ripple logo, images
and image templates are the property of Ripple Labs and cannot be copied or
used without permission.
