rippled Version 0.30.1
============================

rippled 0.30.1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.30.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit c717006c44126aa0edb3a36ca29ee30e7a72c1d3
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Wed Feb 3 14:49:07 2016 -0800

       Set version to 0.30.1

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.30.1) for more detailed information.

**Release Overview**

The rippled team is proud to release rippled version 0.30.1. This version contains a several minor new features as well as significant improvements to the consensus algorithm that make it work faster and with more consistency. In the time we have been testing the new release on our validators, these changes have led to increased agreement and shorter close times between ledger versions, for approximately 40% more ledgers validated per day.

**New Features**

-   Secure gateway: configured IPs can forward identifying user data in HTTP headers, including user name and origin IP. If the user name exists, then resource limits are lifted for that session. See rippled-example.cfg for more information.
-   Allow fractional fee multipliers (RIPD-626)
-   Add “expiration” to account\_offers (RIPD-1049)
-   Add “owner\_funds” to “transactions” array in RPC ledger (RIPD-1050)
-   Add "tx" option to "ledger" command line
-   Add server uptime in server\_info
-   Allow multiple incoming connections from the same IP
-   Report connection uptime in peer command (RIPD-927)
-   Permit pathfinding to be disabled by setting \[path\_search\_max\] to 0 in rippled.cfg file (RIPD-271)
-   Add subscription to peer status changes (RIPD-579)

**Improvements**

-   Improvements to ledger\_request response
-   Improvements to validations proposal relaying (RIPD-1057)
-   Improvements to consensus algorithm
-   Ledger close time optimizations (RIPD-998, RIPD-791)
-   Delete unfunded offers in predictable order

**Development-Related Updates**

-   Require boost 1.57
-   Implement new coroutines (RIPD-1043)
-   Force STAccount interface to 160-bit size (RIPD-994)
-   Improve compile-time OpenSSL version check

**Bug Fixes**

-   Fix handling of secp256r1 signatures (RIPD-1040)
-   Fix websocket messages dispatching
-   Fix pathfinding early response (RIPD-1064)
-   Handle account\_objects empty response (RIPD-958)
-   Fix delivered\_amount reporting for minor ledgers (RIPD-1051)
-   Fix setting admin privileges on websocket
-   Fix race conditions in account\_tx command (RIPD-1035)
-   Fix to enforce no-ripple constraints

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.30.0
============================

rippled 0.30.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.30.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit a8859b495b552fe1eb140771f0f2cb32d11d2ac2
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Wed Oct 21 18:26:02 2015 -0700

        Set version to 0.30.0

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.30.0) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward protocol security, the rippled team would like to release rippled 0.30.0.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**`grep '^processor' /proc/cpuinfo | wc -l`**), you can use them to assist in the build process by compiling with the command **`scons -j[number of CPUs - 1]`**.

**New Features**

-   Honor markers in ledger\_data requests ([RIPD-1010](https://ripplelabs.atlassian.net/browse/RIPD-1010)).
-   New Amendment - **TrustSetAuth** (Not currently enabled) Create zero balance trust lines with auth flag ([RIPD-1003](https://ripplelabs.atlassian.net/browse/RIPD-1003)): this allows a TrustSet transaction to create a trust line if the only thing being changed is setting the tfSetfAuth flag.
-   Randomize the initial transaction execution order for closed ledgers based on the hash of the consensus set ([RIPD-961](https://ripplelabs.atlassian.net/browse/RIPD-961)). **Activates on October 27, 2015 at 11:00 AM PCT**.
-   Differentiate path\_find response ([RIPD-1013](https://ripplelabs.atlassian.net/browse/RIPD-1013)).
-   Convert all of an asset ([RIPD-655](https://ripplelabs.atlassian.net/browse/RIPD-655)).

**Improvements**

-   SHAMap improvements.
-   Upgrade SQLite from 3.8.8.2 to 3.8.11.1.
-   Limit the number of offers that can be consumed during crossing ([RIPD-1026](https://ripplelabs.atlassian.net/browse/RIPD-1026)).
-   Remove unfunded offers on tecOVERSIZE ([RIPD-1026](https://ripplelabs.atlassian.net/browse/RIPD-1026)).
-   Improve transport security ([RIPD-1029](https://ripplelabs.atlassian.net/browse/RIPD-1029)): to take full advantage of the improved transport security, servers with a single, static public IP address should add it to their configuration file, as follows:

     [overlay]
     public_ip=<ip_address>

**Development-Related Updates**

-   Transitional support for gcc 5.2: to enable support define the environmental variable `RIPPLED_OLD_GCC_ABI`=1
-   Transitional support for C++ 14: to enable support define the environment variable `RIPPLED_USE_CPP_14`=1
-   Visual Studio 2015 support
-   Updates to integration tests
-   Add uptime to crawl data ([RIPD-997](https://ripplelabs.atlassian.net/browse/RIPD-997))

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.29.0
============================

rippled 0.29.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/commits/0.29.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 5964710f736e258c7892e8b848c48952a4c7856c
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Tue Aug 4 13:22:45 2015 -0700

        Set version to 0.29.0

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.29.0) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward protocol security, the rippled team would like to announce rippled release 0.29.0.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

**New Features**

-   Subscription stream for validations ([RIPD-504](https://ripplelabs.atlassian.net/browse/RIPD-504))

**Deprecated features**

-   Disable Websocket ping timer

**Bug Fixes**

-   Fix off-by one bug that overstates the account reserve during OfferCreate transaction. **Activates August 17, 2015**.
-   Fix funded offer removal during Payment transaction ([RIPD-113](https://ripplelabs.atlassian.net/browse/RIPD-113)). **Activates August 17, 2015**.
-   Fix display discrepancy in fee.

**Improvements**

-   Add “quality” field to account\_offers API response: quality is defined as the exchange rate, the ratio taker\_pays divided by taker\_gets.
-   Add [full\_reply](https://ripple.com/build/rippled-apis/#path-find-create) field to path\_find API response: full\_reply is defined as true/false value depending on the completed depth of pathfinding search ([RIPD-894](https://ripplelabs.atlassian.net/browse/RIPD-894)).
-   Add [DeliverMin](https://ripple.com/build/transactions/#payment) transaction field ([RIPD-930](https://ripplelabs.atlassian.net/browse/RIPD-930)). **Activates August 17, 2015**.

**Development-Related Updates**

-   Add uptime to crawl data ([RIPD-997](https://ripplelabs.atlassian.net/browse/RIPD-997)).
-   Add IOUAmount and XRPAmount: these numeric types replace the monolithic functionality found in STAmount ([RIPD-976](https://ripplelabs.atlassian.net/browse/RIPD-976)).
-   Log metadata differences on built ledger mismatch.
-   Add enableTesting flag to applyTransactions.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.28.2
============================

rippled 0.28.2 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/commits/release>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 6374aad9bc94595e051a04e23580617bc1aaf300
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Tue Jul 7 09:21:44 2015 -0700

        Set version to 0.28.2

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/release) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward protocol security, the rippled team would like to announce rippled release 0.28.2. **This release is necessary for compatibility with OpenSSL v.1.0.1n and later.**

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**rippled.cfg Updates**

For \[ips\] stanza, a port must be specified for each listed IP address with the space between IP address and port, ex.: `r.ripple.com` `51235` ([RIPD-893](https://ripplelabs.atlassian.net/browse/RIPD-893))

**New Features**

-   New API: [gateway\_balances](https://ripple.com/build/rippled-apis/#gateway-balances) to get a gateway's hot wallet balances and total obligations.

**Deprecated features**

-   Removed temp\_db ([RIPD-887](https://ripplelabs.atlassian.net/browse/RIPD-887))

**Improvements**

-   Improve peer send queue management
-   Support larger EDH keys
-   More robust call to get the valid ledger index
-   Performance improvements to transactions application to open ledger

**Development-Related Updates**

-   New Env transaction testing framework for unit testing
-   Fix MSVC link
-   C++ 14 readiness

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.28.1
============================

rippled 0.28.1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.28.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 399c43cae6e90a428e9ce6a988123972b0f03c99
     Author: Miguel Portilla <miguelportilla@pobros.com>
     Date:   Wed May 20 13:30:54 2015 -0400

        Set version to 0.28.1

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.28.1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Filtering for Account Objects ([RIPD-868](https://ripplelabs.atlassian.net/browse/RIPD-868)).
-   Track rippled server peers latency ([RIPD-879](https://ripplelabs.atlassian.net/browse/RIPD-879)).

**Bug fixes**

-   Expedite zero flow handling for offers
-   Fix offer crossing when funds are the limiting factor

**Deprecated features**

-   Wallet\_accounts and generator maps ([RIPD-804](https://ripplelabs.atlassian.net/browse/RIPD-804))

**Improvements**

-   Control ledger query depth based on peers latency
-   Improvements to ledger history fetches
-   Improve RPC ledger synchronization requirements ([RIPD-27](https://ripplelabs.atlassian.net/browse/RIPD-27), [RIPD-840](https://ripplelabs.atlassian.net/browse/RIPD-840))
-   Eliminate need for ledger in delivered\_amount calculation ([RIPD-860](https://ripplelabs.atlassian.net/browse/RIPD-860))
-   Improvements to JSON parsing

**Development-Related Updates**

-   Add historical ledger fetches per minute to get\_counts
-   Compute validated ledger age from signing time
-   Remove unused database table ([RIPD-755](https://ripplelabs.atlassian.net/browse/RIPD-755))

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.28.0
============================

rippled 0.28.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.28.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 7efd0ab0d6ef017331a0e214a3053893c88f38a9
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Fri Apr 24 18:57:36 2015 -0700

        Set version to 0.28.0

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.28.0) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward improving the protocol, the rippled team is excited to announce **autobridging** — a feature that allows XRP to serve as a bridge currency. Autobridging enhances utility and has the potential to expose more of the network to liquidity and improve prices. For more information please refer to the [autobridging blog post](https://ripple.com/uncategorized/introducing-offer-autobridging/).

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Important rippled.cfg update**

With rippled version 0.28, the rippled.cfg file must be changed according to these instructions:

-   Change any entries that say

`admin` `=` `allow` to `admin` `=` <IP address>

-   For most installations, 127.0.0.1 will preserve current behavior. 0.0.0.0 may be specified to indicate "any IP" but cannot be combined with other IP addresses. Use of 0.0.0.0 may introduce severe security risks and is not recommended. See docs/rippled-example.cfg for more information.

**More Strict Requirements on MemoType**

The requirements on the contents of the MemoType field, if present, are more strict than the previous version. Transactions that can successfully be submitted to 0.27.4 and earlier may fail in 0.28.0. For details, please refer to [updated memo documentation](https://ripple.com/build/transactions/#memos) for details. Partners should check their implementation to make sure that their MemoType follows the new rules.

**New Features**

-   Autobridging implementation ([RIPD-423](https://ripplelabs.atlassian.net/browse/RIPD-423)). **This feature will be turned on May 12, 2015**.
-   Combine history\_ledger\_index and online\_delete settings in rippled.cfg ([RIPD-774](https://ripplelabs.atlassian.net/browse/RIPD-774)).
-   Claim a fee when a required destination tag is not specified ([RIPD-574](https://ripplelabs.atlassian.net/browse/RIPD-574)).
-   Require the master key when disabling the use of the master key or when enabling 'no freeze' ([RIPD-666](https://ripplelabs.atlassian.net/browse/RIPD-666)).
-   Change the port setting admin to accept allowable admin IP addresses ([RIPD-820](https://ripplelabs.atlassian.net/browse/RIPD-820)):
    -   rpc\_admin\_allow has been removed.
    -   Comma-separated list of IP addresses that are allowed administrative privileges (subject to username & password authentication if configured).
    -   127.0.0.1 is no longer a default admin IP.
    -   0.0.0.0 may be specified to indicate "any IP" but cannot be combined with other MIP addresses. Use of 0.0.0.0 may introduce severe security risks and is not recommended.
-   Enable Amendments from config file or static data ([RIPD-746](https://ripplelabs.atlassian.net/browse/RIPD-746)).

**Bug fixes**

-   Fix payment engine handling of offer ⇔ account ⇔ offer cases ([RIPD-639](https://ripplelabs.atlassian.net/browse/RIPD-639)). **This fix will take effect on May 12, 2015**.
-   Fix specified destination issuer in pathfinding ([RIPD-812](https://ripplelabs.atlassian.net/browse/RIPD-812)).
-   Only report delivered\_amount for executed payments ([RIPD-827](https://ripplelabs.atlassian.net/browse/RIPD-827)).
-   Return a validated ledger if there is one ([RIPD-814](https://ripplelabs.atlassian.net/browse/RIPD-814)).
-   Refund owner's ticket reserve when a ticket is canceled ([RIPD-855](https://ripplelabs.atlassian.net/browse/RIPD-855)).
-   Return descriptive error from account\_currencies RPC ([RIPD-806](https://ripplelabs.atlassian.net/browse/RIPD-806)).
-   Fix transaction enumeration in account\_tx API ([RIPD-734](https://ripplelabs.atlassian.net/browse/RIPD-734)).
-   Fix inconsistent ledger\_current return ([RIPD-669](https://ripplelabs.atlassian.net/browse/RIPD-669)).
-   Fix flags --rpc\_ip and --rpc\_port ([RIPD-679](https://ripplelabs.atlassian.net/browse/RIPD-679)).
-   Skip inefficient SQL query ([RIPD-870](https://ripplelabs.atlassian.net/browse/RIPD-870))

**Deprecated features**

-   Remove support for deprecated PreviousTxnID field ([RIPD-710](https://ripplelabs.atlassian.net/browse/RIPD-710)). **This will take effect on May 12, 2015**.
-   Eliminate temREDUNDANT\_SEND\_MAX ([RIPD-690](https://ripplelabs.atlassian.net/browse/RIPD-690)).
-   Remove WalletAdd ([RIPD-725](https://ripplelabs.atlassian.net/browse/RIPD-725)).
-   Remove SMS support.

**Improvements**

-   Improvements to peer communications.
-   Reduce master lock for client requests.
-   Update SQLite to 3.8.8.2.
-   Require Boost 1.57.
-   Improvements to Universal Port ([RIPD-687](https://ripplelabs.atlassian.net/browse/RIPD-687)).
-   Constrain valid inputs for memo fields ([RIPD-712](https://ripplelabs.atlassian.net/browse/RIPD-712)).
-   Binary option for ledger command ([RIPD-692](https://ripplelabs.atlassian.net/browse/RIPD-692)).
-   Optimize transaction checks ([RIPD-751](https://ripplelabs.atlassian.net/browse/RIPD-751)).

**Development-Related Updates**

-   Add RPC metrics ([RIPD-705](https://ripplelabs.atlassian.net/browse/RIPD-705)).
-   Track and report peer load.
-   Builds/Test.py will build and test by one or more scons targets.
-   Support a --noserver command line option in tests:
-   Run npm/integration tests without launching rippled, using a running instance of rippled (possibly in a debugger) instead.
-   Works for npm test and mocha.
-   Display human readable SSL error codes.
-   Better transaction analysis ([RIPD-755](https://ripplelabs.atlassian.net/browse/RIPD-755)).

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.27.4
============================

rippled 0.27.4 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.4>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 92812fe7239ffa3ba91649b2ece1e892b866ec2a
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Wed Mar 11 11:26:44 2015 -0700

        Set version to 0.27.4

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.4) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Bug Fixes**

-   Limit passes in the payment engine

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.27.3-sp2
============================

rippled 0.27.3-sp2 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.3-sp2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit f999839e599e131ed624330ad0ce85bb995f02d3
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Thu Mar 12 13:37:47 2015 -0700

        Set version to 0.27.3-sp2

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.3-sp2) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Add noripple\_check RPC command: this command tells gateways what they need to do to set "Default Ripple" account flag and fix any trust lines created before the flag was set.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

rippled Version 0.27.3-sp1
============================

rippled 0.27.3-sp1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.3-sp1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 232693419a2c9a8276a0fae991f688f6f01a3add
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Wed Mar 11 10:26:39 2015 -0700

       Set version to 0.27.3-sp1

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.3-sp1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Add "Default Ripple" account flag

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.27.3
============================

rippled 0.27.3 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.3>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 70c2854f7c8a28801a7ebc81dd62bf0d068188f0
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Tue Mar 10 14:06:33 2015 -0700

        Set version to 0.27.3

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.3) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Add "Default Ripple" account flag

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.27.2
============================

rippled 0.27.2 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 9cc8eec773e8afc9c12a6aab4982deda80495cf1
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Sun Mar 1 14:56:44 2015 -0800

       Set version to 0.27.2

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.2) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   NuDB backend option: high performance key/value database optimized for rippled (set “type=nudb” in .cfg).
    -   Either import RockdDB to NuDB using import tool, or
    -   Start fresh with NuDB but delete SQLite databases if rippled ran previously with RocksDB:

     rm [database_path]/transaction.* [database_path]/ledger.*

**Bug Fixes**

-   Fix offer quality bug

**Deprecated**

-   HyperLevelDB, LevelDB, and SQLlite backend options. Use RocksDB for spinning drives and NuDB for SSDs backend options.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.27.1
============================

rippled 0.27.1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 95973ba3e8b0bd28eeaa034da8b806faaf498d8a
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Tue Feb 24 13:31:13 2015 -0800

       Set version to 0.27.1

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   RocksDB to NuDB import tool ([RIPD-781](https://ripplelabs.atlassian.net/browse/RIPD-781), [RIPD-785](https://ripplelabs.atlassian.net/browse/RIPD-785)): custom tool specifically designed for very fast import of RocksDB nodestore databases into NuDB

**Bug Fixes**

-   Fix streambuf bug

**Improvements**

-   Update RocksDB backend settings
-   NuDB improvements:
    -   Limit size of mempool ([RIPD-787](https://ripplelabs.atlassian.net/browse/RIPD-787))
    -   Performance improvements ([RIPD-793](https://ripplelabs.atlassian.net/browse/RIPD-793), [RIPD-796](https://ripplelabs.atlassian.net/browse/RIPD-796)): changes in Nudb to improve speed, reduce database size, and enhance correctness. The most significant change is to store hashes rather than entire keys in the key file. The output of the hash function is reduced to 48 bits, and stored directly in buckets.

**Experimental**

-   Add /crawl cgi request feature to peer protocol ([RIPD-729](https://ripplelabs.atlassian.net/browse/RIPD-729)): adds support for a cgi /crawl request, issued over HTTPS to the configured peer protocol port. The response to the request is a JSON object containing the node public key, type, and IP address of each directly connected neighbor. The IP address is suppressed unless the neighbor has requested its address to be revealed by adding "Crawl: public" to its HTTP headers. This field is currently set by the peer\_private option in the rippled.cfg file.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.27.0
============================

rippled 0.27.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit c6c8e5d70c6fbde02cd946135a061aa77744396f
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Jan 26 10:56:11 2015 -0800

         Set version to 0.27.0

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.0) for more detailed information.

**Release Overview**

The rippled team is proud to release rippled 0.27.0. This new version includes many exciting features that will appeal to our users. The team continues to work on stability, scalability, and performance.

The first feature is Online Delete. This feature allows rippled to maintain it’s database of previous ledgers within a fixed amount of disk space. It does this while allowing rippled to stay online and maintain an administrator specify minimum number of ledgers. This means administrators with limited disk space will no longer need to manage disk space by periodically manually removing the database. Also, with the previously existing backend databases performance would gradually degrade as the database grew in size. In particular, rippled would perform poorly whenever the backend database performed ever growing compaction operations. By limiting rippled to less history, compaction is less resource intensive and systems with less disk performance can now run rippled.

Additionally, we are very excited to include Universal Port. This feature allows rippled's listening port to handshake in multiple protocols. For example, a single listening port can be configured to receive incoming peer connections, incoming RPC commands over HTTP, and incoming RPC commands over HTTPS at the same time. Or, a single port can receive both Websockets and Secure Websockets clients at the same.

Finally, a new, experimental backend database, NuDB, has been added. This database was developed by Ripple Labs to take advantage of rippled’s specific data usage profile and performs much better than previous databases. Significantly, this database does not degrade in performance as the database grows. Very excitingly, this database works on OS X and Windows. This allows rippled to use these platforms for the first time.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Important rippled.cfg Update**

**The format of the configuration file has changed. If upgrading from a previous version of rippled, please see the migration instructions below.**

**New Features**

-   SHAMapStore Online Delete ([RIPD-415](https://ripplelabs.atlassian.net/browse/RIPD-415)): Makes rippled configurable to support deletion of all data in its key-value store (nodestore) and ledger and transaction SQLite databases based on validated ledger sequence numbers. See doc/rippled-example.cfg for configuration setup.
-   [Universal Port](https://forum.ripple.com/viewtopic.php?f=2&t=8313&p=57969). See necessary config changes below.
-   Config "ledger\_history\_index" option ([RIPD-559](https://ripplelabs.atlassian.net/browse/RIPD-559))

**Bug Fixes**

-   Fix pathfinding with multiple issuers for one currency ([RIPD-618](https://ripplelabs.atlassian.net/browse/RIPD-618))
-   Fix account\_lines, account\_offers and book\_offers result ([RIPD-682](https://ripplelabs.atlassian.net/browse/RIPD-682))
-   Fix pathfinding bugs ([RIPD-735](https://ripplelabs.atlassian.net/browse/RIPD-735))
-   Fix RPC subscribe with multiple books ([RIPD-77](https://ripplelabs.atlassian.net/browse/RIPD-77))
-   Fix account\_tx API

**Improvements**

-   Improve the human-readable description of the tesSUCCESS code
-   Add 'delivered\_amount' to Transaction JSON ([RIPD-643](https://ripplelabs.atlassian.net/browse/RIPD-643)): The synthetic field 'delivered\_amount' can be used to determine the exact amount delivered by a Payment without having to check the DeliveredAmount field, if present, or the Amount field otherwise.

**Development-Related Updates**

-   HTTP Handshaking for Peers on Universal Port ([RIPD-446](https://ripplelabs.atlassian.net/browse/RIPD-446))
-   Use asio signal handling in Application ([RIPD-140](https://ripplelabs.atlassian.net/browse/RIPD-140))
-   Build dependency on Boost 1.57.0
-   Support a "no\_server" flag in test config
-   API for improved Unit Testing ([RIPD-432](https://ripplelabs.atlassian.net/browse/RIPD-432))
-   Option to specify rippled path on command line (--rippled=\<absolute or relative path\>)

**Experimental**

-   NuDB backend option: high performance key/value database optimized for rippled (set “type=nudb” in .cfg)

**Migration Instructions**

With rippled version 0.27.0, the rippled.cfg file must be changed according to these instructions:

-   Add new stanza - `[server]`. This section will contain a list of port names and key/value pairs. A port name must start with a letter and contain only letters and numbers. The name is not case-sensitive. For each name in this list, rippled will look for a configuration file section with the same name and use it to create a listening port. To simplify migration, you can use port names from your previous version of rippled.cfg (see Section 1. Server for detailed explanation in doc/rippled-example.cfg). For example:

     [server]
     rpc_port
     peer_port
     websocket_port
     ssl_key = <set value to your current [rpc_ssl_key] or [websocket_ssl_key] setting>
     ssl_cert = <set value to your current [rpc_ssl_cert] or [websocket_ssl_cert] setting>
     ssl_chain = <set value to your current [rpc_ssl_chain] or [websocket_ssl_chain] setting>

-   For each port name in `[server]` stanza, add separate stanzas. For example:

     [rpc_port]
     port = <set value to your current [rpc_port] setting, usually 5005>
     ip = <set value to your current [rpc_ip] setting, usually 127.0.0.1>
     admin = allow
     protocol = https

     [peer_port]
     port = <set value to your current [peer_port], usually 51235>
     ip = <set value to your current [peer_ip], usually 0.0.0.0>
     protocol = peer

     [websocket_port]
     port = <your current [websocket_port], usually 6006>
     ip = <your current [websocket_ip], usually 127.0.0.1>
     admin = allow
     protocol = wss

-   Remove current `[rpc_port],` `[rpc_ip],` `[rpc_allow_remote],` `[rpc_ssl_key],` `[rpc_ssl_cert],` `and` `[rpc_ssl_chain],` `[peer_port],` `[peer_ip],` `[websocket_port],` `[websocket_ip]` settings from rippled.cfg

-   If you allow untrusted websocket connections to your rippled, add `[websocket_public_port]` stanza under `[server]` section and replace websocket public settings with `[websocket_public_port]` section:

     [websocket_public_port]
     port = <your current [websocket_public_port], usually 5005>
     ip = <your current [websocket_public_ip], usually 127.0.0.1>
     protocol = ws ← make sure this is ws, not wss`

-   Remove `[websocket_public_port],` `[websocket_public_ip],` `[websocket_ssl_key],` `[websocket_ssl_cert],` `[websocket_ssl_chain]` settings from rippled.cfg
-   Disable `[ssl_verify]` section by setting it to 0
-   Migrate the remaining configurations without changes. To enable online delete feature, check Section 6. Database in doc/rippled-example.cfg

**Integration Notes**

With this release, integrators should deprecate the "DeliveredAmount" field in favor of "delivered\_amount."

### For Transactions That Occurred Before January 20, 2014: ###

-   If amount actually delivered is different than the transactions “Amount” field
    -   "delivered\_amount" will show as unavailable indicating a developer should use caution when processing this payment.
    -   Example: A partial payment transaction (tfPartialPayment).
-   Otherwise
    -   "delivered\_amount" will show the correct destination balance change.

### For Transactions That Occur After January 20, 2014: ###

-   If amount actually delivered is different than the transactions “Amount” field
    -   A "delivered\_amount" field will determine the destination amount change
    -   Example: A partial payment transaction (tfPartialPayment).
-   Otherwise
    -   "delivered\_amount" will show the correct destination balance change.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.26.4
============================

rippled 0.26.4 has been released. The repository tag is *0.26.4* and can be found on GitHub at: <https://github.com/ripple/rippled/commits/0.26.4>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 05a04aa80192452475888479c84ff4b9b54e6ae7
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Nov 3 16:53:37 2014 -0800

         Set version to 0.26.4

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.4) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Important JSON-RPC Update**

With rippled version 0.26.4, the [rippled.cfg](https://github.com/ripple/rippled/blob/0.26.4/doc/rippled-example.cfg) file must set the ssl\_verify property to 0. Without this update, JSON-RPC API calls may not work.

**New Features**

-   Rocksdb v. 3.5.1
-   SQLite v. 3.8.7
-   Disable SSLv3
-   Add counters to track ledger read and write activities
-   Use trusted validators median fee when determining transaction fee
-   Add --quorum argument for server start ([RIPD-563](https://ripplelabs.atlassian.net/browse/RIPD-563))
-   Add account\_offers paging ([RIPD-344](https://ripplelabs.atlassian.net/browse/RIPD-344))
-   Add account\_lines paging ([RIPD-343](https://ripplelabs.atlassian.net/browse/RIPD-343))
-   Ability to configure network fee in rippled.cfg file ([RIPD-564](https://ripplelabs.atlassian.net/browse/RIPD-564))

**Bug Fixes**

-   Fix OS X version parsing/error related to OS X 10.10 update
-   Fix incorrect address in connectivity check report
-   Fix page sizes for ledger\_data ([RIPD-249](https://ripplelabs.atlassian.net/browse/RIPD-249))
-   Make log partitions case-insensitive in rippled.cfg

**Improvements**

-   Performance
    -   Ledger performance improvements for storage and traversal ([RIPD-434](https://ripplelabs.atlassian.net/browse/RIPD-434))
    -   Improve client performance for JSON responses ([RIPD-439](https://ripplelabs.atlassian.net/browse/RIPD-439))
-   Other
    -   Remove PROXY handshake feature
    -   Change to rippled.cfg to support sections containing both key/value pairs and a list of values
    -   Return descriptive error message for memo validation ([RIPD-591](https://ripplelabs.atlassian.net/browse/RIPD-591))
    -   Changes to enforce JSON-RPC 2.0 error format
    -   Optimize account\_lines and account\_offers ([RIPD-587](https://ripplelabs.atlassian.net/browse/RIPD-587))
    -   Improve fee setting logic ([RIPD-614](https://ripplelabs.atlassian.net/browse/RIPD-614))
    -   Improve transaction security
    -   Config improvements
    -   Improve path filtering ([RIPD-561](https://ripplelabs.atlassian.net/browse/RIPD-561))
    -   Logging to distinguish Byzantine failure from tx bug ([RIPD-523](https://ripplelabs.atlassian.net/browse/RIPD-523))

**Experimental**

-   Add "deferred" flag to transaction relay message (required for future code that will relay deferred transactions)
-   Refactor STParsedJSON to parse an object or array (required for multisign implementation) ([RIPD-480](https://ripplelabs.atlassian.net/browse/RIPD-480))

**Development-Related Updates**

-   Changes to DatabaseReader to read ledger numbers from database
-   Improvements to SConstruct

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.26.3-sp1
============================

rippled 0.26.3-sp1 has been released. The repository tag is *0.26.3-sp1* and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.3-sp1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 2ad6f0a65e248b4f614d38d199a9d5d02f5aaed8
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Fri Sep 12 15:22:54 2014 -0700

         Set version to 0.26.3-sp1

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.3-sp1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   New command to display HTTP/S-RPC sessions metrics ([RIPD-533](https://ripplelabs.atlassian.net/browse/RIPD-533))

**Bug Fixes**

-   Improved handling of HTTP/S-RPC sessions ([RIPD-489](https://ripplelabs.atlassian.net/browse/RIPD-489))
-   Fix unit tests for Windows.
-   Fix integer overflows in JSON parser.

**Improvements**

-   Improve processing of trust lines during pathfinding.

**Experimental Features**

-   Added a command line utility called LedgerTool for retrieving and processing ledger blocks from the Ripple network.

**Development-Related Updates**

-   HTTP message and parser improvements.
    -   Streambuf wrapper supports rvalue move.
    -   Message class holds a complete HTTP message.
    -   Body class holds the HTTP content body.
    -   Headers class holds RFC-compliant HTTP headers.
    -   Basic\_parser provides class interface to joyent's http-parser.
    -   Parser class parses into a message object.
    -   Remove unused http get client free function.
    -   Unit test for parsing malformed messages.
-   Add enable\_if\_lvalue.
-   Updates to includes and scons.
-   Additional ledger.history.mismatch insight statistic.
-   Convert rvalue to an lvalue. ([RIPD-494](https://ripplelabs.atlassian.net/browse/RIPD-494))
-   Enable heap profiling with jemalloc.
-   Add aged containers to Validators module. ([RIPD-349](https://ripplelabs.atlassian.net/browse/RIPD-349))
-   Account for high-ASCII characters. ([RIPD-464](https://ripplelabs.atlassian.net/browse/RIPD-464))

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.26.2
============================

rippled 0.26.2 has been released. The repository tag is *0.26.2* and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit b9454e0f0ca8dbc23844a0520d49394e10d445b1
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Aug 11 15:25:44 2014 -0400

        Set version to 0.26.2

This release incorporates a small number of important bugfixes. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.2) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Freeze enforcement: activates on September 15, 2014 ([RIPD-399](https://ripplelabs.atlassian.net/browse/RIPD-399))
-   Add pubkey\_node and hostid to server stream messages ([RIPD-407](https://ripplelabs.atlassian.net/browse/RIPD-407))

**Bug Fixes**

-   Fix intermittent exception when closing HTTPS connections ([RIPD-475](https://ripplelabs.atlassian.net/browse/RIPD-475))
-   Correct Pathfinder::getPaths out to handle order books ([RIPD-427](https://ripplelabs.atlassian.net/browse/RIPD-427))
-   Detect inconsistency in PeerFinder self-connects ([RIPD-411](https://ripplelabs.atlassian.net/browse/RIPD-411))

**Experimental Features**

-   Add owner\_funds to client subscription data ([RIPD-377](https://ripplelabs.atlassian.net/browse/RIPD-377))

The offer funding status feature is “experimental” in this version. Developers are able to see the field, but it is subject to change in future releases.

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.26.1
============================

rippled v0.26.1 has been released. The repository tag is **0.26.1** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 9a0e806f78300374e20070e2573755fbafdbfd03
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Jul 28 11:27:31 2014 -0700

         Set version to 0.26.1

This release incorporates a small number of important bugfixes. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Bug Fixes**

-   Enabled asynchronous handling of HTTP-RPC interactions. This fixes client handlers using RPC that periodically return blank responses to requests. ([RIPD-390](https://ripplelabs.atlassian.net/browse/RIPD-390))
-   Fixed auth handling during OfferCreate. This fixes a regression of [RIPD-256](https://ripplelabs.atlassian.net/browse/RIPD-256). ([RIPD-414](https://ripplelabs.atlassian.net/browse/RIPD-414))

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.26.0
============================

rippled v0.26.0 has been released. The repository tag is **0.26.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 9fa5e3987260e39dba322f218d39ac228a5b361b
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Tue Jul 22 09:59:45 2014 -0700

         Set version to 0.26.0

This release incorporates a significant number of improvements and important bugfixes. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   Updated integration tests.
-   Updated tests for account freeze functionality.
-   Implement setting the no-freeze flag on Ripple accounts ([RIPD-394](https://ripplelabs.atlassian.net/browse/RIPD-394)).
-   Improve transaction fee and execution logic ([RIPD-323](https://ripplelabs.atlassian.net/browse/RIPD-323)).
-   Implemented finding of 'sabfd' paths ([RIPD-335](https://ripplelabs.atlassian.net/browse/RIPD-335)).
-   Imposed a local limit on paths lengths ([RIPD-350](https://ripplelabs.atlassian.net/browse/RIPD-350)).
-   Documented [ledger entries](https://github.com/ripple/rippled/blob/develop/src/ripple/module/app/ledger/README.md) ([RIPD-361](https://ripplelabs.atlassian.net/browse/RIPD-361)).
-   Documented [SHAMap](https://github.com/ripple/rippled/blob/develop/src/ripple/module/app/shamap/README.md).

**Bug Fixes**

-   Fixed the limit parameter on book\_offers ([RIPD-295](https://ripplelabs.atlassian.net/browse/RIPD-295)).
-   Removed SHAMapNodeID from SHAMapTreeNode to fix "right data, wrong ID" bug in the tree node cache ([RIPD-347](https://ripplelabs.atlassian.net/browse/RIPD-347)).
-   Eliminated spurious SHAMap::getFetchPack failure ([RIPD-379](https://ripplelabs.atlassian.net/browse/RIPD-379)).
-   Disabled SSLv2.
-   Implemented rate-limiting of SSL client renegotiation to mitigate [SCIR DoS vulnerability](https://www.thc.org/thc-ssl-dos/) ([RIPD-360](https://ripplelabs.atlassian.net/browse/RIPD-360)).
-   Display unprintable or malformatted currency codes as hex digits.
-   Fix static initializers in RippleSSLContext ([RIPD-375](https://ripplelabs.atlassian.net/browse/RIPD-375)).

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

rippled Version 0.25.2
============================

rippled v0.25.2 has been released. The repository tag is **0.25.2** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.25.2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit ddf68d464d74e1c76a0cfd100a08bc8e65b91fec
     Author: Mark Travis <mtravis@ripple.com>
     Date:   Mon Jul 7 11:46:15 2014 -0700

         Set version to 0.25.2

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend build machines with 8GB of RAM.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   CPU utilization for certain operations has been optimized.
-   Improve serialization of public ledger blocks.
-   rippled now takes much less time to compile.
-   Additional pathfinding heuristic: increases liquidity in some cases.

**Bug Fixes**

-   Unprintable currency codes will be printed as hex digits.
-   Transactions with unreasonably long path lengths are rejected. The maximum is now eight (8) hops.


-----------------------------------------------------------

rippled Version 0.25.1
============================

`rippled` v0.25.1 has been released. The repository tag is `0.25.1` and can be found on GitHub at: https://github.com/ripple/rippled/tree/0.25.1

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

     commit b677cacb8ce0d4ef21f8c60112af1db51dce5bb4
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Thu May 15 08:27:20 2014 -0700

         Set version to 0.25.1

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines.  Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8.  Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55.  You **must** upgrade to this release or later to successfully compile this release.  Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Major Features**

* Option to compress the NodeStore db. More speed, less space. See [`rippled-example.cfg`](https://github.com/ripple/rippled/blob/0.25.1/doc/rippled-example.cfg#L691)

**Improvements**

* Remove redundant checkAccept call
* Added I/O latency to output of ''server_info''.
* Better performance handling of Fetch Packs.
* Improved handling of modified ledger nodes.
* Improved performance of JSON document generator.
* Made strConcat operate in O(n) time for greater efficiency.
* Added some new configuration options to doc/rippled-example.cfg

**Bug Fixes**

* Fixed a bug in Unicode parsing of transactions.
* Fix a blocker with tfRequireAuth
* Merkle tree nodes that are retrieved as a result of client requests are cached locally.
* Use the last ledger node closed for finding old paths through the network.
* Reduced number of asynchronous fetches.


-----------------------------------------------------------

rippled Version 0.25.0
============================

rippled version 0.25.0 has been released. The repository tag is **0.25.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.25.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 29d1d5f06261a93c5e94b4011c7675ff42443b7f
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Wed May 14 09:01:44 2014 -0700

         Set version to 0.25.0

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Major Features**

-   Option to compress the NodeStore db. More speed, less space. See [`rippled-example.cfg`](https://github.com/ripple/rippled/blob/0.25.0/doc/rippled-example.cfg#L691)

**Improvements**

-   Remove redundant checkAccept call
-   Added I/O latency to output of *server\_info*.
-   Better performance handling of Fetch Packs.
-   Improved handling of modified ledger nodes.
-   Improved performance of JSON document generator.
-   Made strConcat operate in O(n) time for greater efficiency.

**Bug Fixes**

-   Fix a blocker with tfRequireAuth
-   Merkle tree nodes that are retrieved as a result of client requests are cached locally.
-   Use the last ledger node closed for finding old paths through the network.
-   Reduced number of asynchronous fetches.


-----------------------------------------------------------

rippled Version 0.24.0
============================

rippled version 0.24.0 has been released. The repository tag is **0.24.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.24.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 3eb1c7bd6f93e5d874192197f76571184338f702
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon May 5 10:20:46 2014 -0700

         Set version to 0.24.0

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   Implemented logic for ledger processes and features.
-   Use "high threads" for background RocksDB database writes.
-   Separately track locally-issued transactions to ensure they always appear in the open ledger.

**Bug Fixes**

-   Fix AccountSet for canonical transactions.
-   The RPC [sign](https://ripple.com/build/rippled-apis/#sign) command will now sign with either an account's master or regular secret key.
-   Fixed out-of-order network initialization.
-   Improved efficiency of pathfinding for transactions.
-   Reworked timing of ledger validation and related operations to fix race condition against the network.
-   Build process enforces minimum versions of OpenSSL and BOOST for operation.


-----------------------------------------------------------

rippled Version 0.23.0
============================

rippled version 0.23.0 has been released. The repository tag is **0.23.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.23.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 29a4f61551236f70865d46d6653da2e62de1c701
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Fri Mar 14 13:01:23 2014 -0700

         Set version to 0.23.0

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   Allow the word 'none' in the *.cfg* file to disable storing historical ledgers.
-   Clarify the initialization of hash prefixes used in the *RadMap*.
-   Better validation of RPC-JSON from all sources
-   Reduce spurious log output from Peers
-   Eliminated some I/O for certain operations in the *RadMap*.
-   Client requests for full state trees now require administrative privileges.
-   Added "MemoData" field for transaction memos.
-   Prevent the node cache from overflowing needlessly in certain cases
-   Add "ledger\_data" command for retrieving entire ledgers in chunks.
-   Reduce the quantity of forwarded transactions and proposals in some cases
-   Improved diagnostics when errors occur loading SSL certificates

**Bug Fixes**

-   Fix rare crash when a race condition involving disconnecting sockets occurs
-   Fix a corner case with hex conversion of strings with odd character lengths
-   Fix an exception in a corner case when erroneous transactions were being logged
-   Fix the treatment of expired offers when cleaning up offers
-   Prevent a needless transactor from being created if the tx ID is not valid
-   Fix the peer action transition from "syncing" to "full"
-   Fix error reporting for unknown inner JSON fields
-   Fix source file path displayed when an assertion failure is reported
-   Fix typos in transaction engine error code identifiers


-----------------------------------------------------------

rippled Version 0.22.0
============================

rippled version 0.22.0 has been released. This release is currently the tip of the **develop/** branch and can be found on GitHub at: <https://github.com/ripple/rippled/tree/develop> The tag is **0.22.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.22.0>

**This is a critical release affecting transaction processing. All partners should update immediately.**

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of libBOOST is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Key release features**

#### PeerFinder

-   Actively guides network topology.
-   Scrubs listening advertisements based on connectivity checks.
-   Redirection for new nodes when existing nodes are full.

#### Memos

-   Transactions can optionally include a short text message, which optionally can be encrypted.

#### Database

-   Improved management of I/O resources.
-   Better performance accessing historical data.

#### PathFinding

-   More efficient search algorithm when computing paths

**Major Partner Issues Fixed**

#### Transactions

-   Malleability: Ability to ensure that signatures are fully canonical.

#### PathFinding

-   Less time needed to get the first path result!

#### Database

-   Eliminated "meltdowns" caused when fetching historical ledger data.

**Significant Changes**

-   Cleaned up logic which controls when ledgers are fetched and under what conditions.
-   Cleaned up file path calculation for database files.
-   Changed dispatcher for WebSocket requests.
-   Cleaned up multithreading mechanisms.
-   Fixed custom currency code parsing.
-   Optimized transaction node lookup circumstances in the node store.


-----------------------------------------------------------

rippled Version 0.21.0
============================

rippled version 0.21.0 has been released. This release is currently the tip of the **develop/** branch and can be found on GitHub at [1](https://github.com/ripple/rippled/tree/develop). The tag is **0.21.0-rc2** and can be found on GitHub at [2](https://github.com/ripple/rippled/tree/0.21.0-rc2).

**This is a critical release. All partners should update immediately.**

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

    commit f295bb20a16d1d2999f606c1297c8930d8e33c40
    Author: JoelKatz <DavidJoelSchwartz@GMail.com>
    Date:   Fri Jan 24 11:17:16 2014 -0800

        Set version to 0.21.0.rc2

**Major Partner Issues Fixed**

-   Order book issues
    -   Ensure all crossing offers are taken
    -   Ensure order book is not left crossed
-   Added **DeliveredAmount** field to transaction metadata
    -   Reports amount delivered in partial payments

**Toolchain support**

As with the previous release, the minimum supported version of GCC used to compile rippled is v4.8.

**Significant Changes**

-   Pairwise no-ripple
    -   Permits trust lines to be protected from rippling
    -   Operates on protected pairs
-   Performance improvements
    -   Improve I/O latency
    -   Improve fetching ledgers
    -   Improve pathfinding
-   Features for robust transaction submission
    -   LastLedgerSeq for transaction expiration
    -   AccountTxnID for transaction chaining
-   Fix some cases where an invalid transaction would stay in limbo
-   Code cleanups
-   Better reporting of invalid parameters

**Release Candidates**

RC1 fixed performance problems with order book retrieval.

RC2 fixed a bug that caused crashes in order processing and a bug in parsing order book requests.

**Notice**

If you are upgrading from version 0.12 or earlier of rippled, these next sections apply to you because the format of the *rippled.cfg* file changed around that time. If you have upgraded since that time and you have applied the configuration file fixes, you can safely ignore them.

### Validators

Ripple Labs is now running five validators. You can use this template for your *validators.txt* file (or place this in your config file):

     [validators]
     n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
     n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
     n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
     n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
     n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your *rippled.cfg* file:

     [validation_quorum]
     3

If you are a validator, you should set your quorum to at least four.

### IPs

A list of Ripple Labs server IP addresses can be found by resolving **r.ripple.com**. You can also add this to your *rippled.cfg* file to ensure you always have several peer connections to Ripple Labs servers:

     [ips]
     184.73.226.101 51235
     23.23.201.55   51235
     54.200.43.173  51235
     184.73.57.84   51235
     54.234.249.55  51235
     54.200.86.110  51235

### RocksDB back end

RocksDB is based on LevelDB with improvements from Facebook and the community. Preliminary tests show that it stalls less often than HyperLevelDB for our use cases.

If you are switching over from an existing back end, you have two options. You can remove your old database and let rippled recreate it as it re-syncs, or you can import your old database into the new one.

To remove your old database, make sure the server is shut down (\`rippled stop\`). Remove the *db/ledger.db* and *db/transaction.db* files. Remove all the files in your back end store directory (*db/hashnode* by default). Then change your configuration file to use the RocksDB back end and restart.

To import your old database, start by shutting the server down. Then modify the configuration file by renaming your *\[node\_db\]* stanza to *\[import\_db\]*. Create a new *\[node\_db\]* stanza and specify a RocksDB back end with a different directory. Start the server with the command **rippled --import**. When the import finishes gracefully stop the server (\`rippled stop\`). Please wait for rippled to stop on its own because it can take several minutes for it to shut down after an import. Remove the old database, put the new database into place, remove the *\[import\_db\]* section, change the *\[node\_db\]* section to refer to the final location, and restart the server.

The recommended RocksDB configuration is:

     [node_db]
     type=RocksDB
     path=db/hashnode
     open_files=1200
     filter_bits=12
     cache_mb=128
     file_size_mb=8
     file_size_mult=2

### Configuring your Node DB

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. See above for an example RocksDB configuration.

-   **Note**: HyperLevelDB and RocksDB are not available on Windows platform.


-----------------------------------------------------------

rippled Version 0.20.1
============================

rippled version 0.20.1 has been released. This release is currently the tip of the [develop](https://github.com/ripple/rippled/tree/develop) branch and the tag is [0.20.1](https://github.com/ripple/rippled/tree/0.20.1).

**This is a critical release. All partners should update immediately.**

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

    commit 95a573b755219d7e1e078d53b8e11a8f0d7cade1
    Author: Vinnie Falco <vinnie.falco@gmail.com>
    Date:   Wed Jan 8 17:08:27 2014 -0800

       Set version to 0.20.1

**Major Partner Issues Fixed**

-   rippled will crash randomly.
    -   Entries in the three parts of the order book are missing or do not match. In such a case, rippled will crash.
-   Server loses sync randomly.
    -   This is due to rippled restarting after it crashes. That the server restarted is not obvious and appears to be something else.
-   Server goes 'offline' randomly.
    -   This is due to rippled restarting after it crashes. That the server restarted is not obvious and appears to be something else.
-   **complete\_ledgers** part of **server\_info** output says "None".
    -   This is due to rippled restarting and reconstructing the ledger after it crashes.
    -   If the node back end is corrupted or has been moved without being renamed in rippled.cfg, this can cause rippled to crash and restart.

**Toolchain support**

Starting with this release, the minimum supported version of GCC used to compile rippled is v4.8.

**Significant Changes**

-   Don't log StatsD messages to the console by default.
-   Fixed missing jtACCEPT job limit.
-   Removed dead code to clean up the codebase.
-   Reset liquidity before retrying rippleCalc.
-   Made improvements becuase items in SHAMaps are immutable.
-   Multiple pathfinding bugfixes:
    -   Make each path request track whether it needs updating.
    -   Improve new request handling, reverse order for processing requests.
    -   Break to handle new requests immediately.
    -   Make mPathFindThread an integer rather than a bool. Allow two threads.
    -   Suspend processing requests if server is backed up.
    -   Multiple performance improvements and enhancements.
    -   Fixed locking.
-   Refactored codebase to make it C++11 compliant.
-   Multiple fixes to ledger acquisition, cleanup, and logging.
-   Made multiple improvements to WebSockets server.
-   Added Debian-style initscript (doc/rippled.init).
-   Updated default config file (doc/rippled-example.cfg) to reflect best practices.
-   Made changes to SHAMapTreeNode and visitLeavesInternal to conserve memory.
-   Implemented new fee schedule:
    -   Transaction fee: 10 drops
    -   Base reserve: 20 XRP
    -   Incremental reserve: 5 XRP
-   Fixed bug \#211 (getTxsAccountB in NetworkOPs).
-   Fixed a store/fetch race condition in ther node back end.
-   Fixed multiple comparison operations.
-   Removed Sophia and Lightning databases.

**Notice**

If you are upgrading from version 0.12 or earlier of rippled, these next sections apply to you because the format of the *rippled.cfg* file changed around that time. If you have upgraded since that time and you have applied the configuration file fixes, you can safely ignore them.

### Validators

Ripple Labs is now running five validators. You can use this template for your *validators.txt* file (or place this in your config file):

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your *rippled.cfg* file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

### IPs

A list of Ripple Labs server IP addresses can be found by resolving **r.ripple.com**. You can also add this to your *rippled.cfg* file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

### New RocksDB back end

RocksDB is based on LevelDB with improvements from Facebook and the community. Preliminary tests show that it stalls less often than HyperLevelDB for our use cases.

If you are switching over from an existing back end, you have two options. You can remove your old database and let rippled recreate it as it re-syncs, or you can import your old database into the new one.

To remove your old database, make sure the server is shut down (`rippled stop`). Remove the *db/ledger.db* and *db/transaction.db* files. Remove all the files in your back end store directory (*db/hashnode* by default). Then change your configuration file to use the RocksDB back end and restart.

To import your old database, start by shutting the server down. Then modify the configuration file by renaming your *\[node\_db\]* stanza to *\[import\_db\]*. Create a new *\[node\_db\]* stanza and specify a RocksDB back end with a different directory. Start the server with the command **rippled --import**. When the import finishes gracefully stop the server (`rippled stop`). Please wait for rippled to stop on its own because it can take several minutes for it to shut down after an import. Remove the old database, put the new database into place, remove the *\[import\_db\]* section, change the *\[node\_db\]* section to refer to the final location, and restart the server.

The recommended RocksDB configuration is:

    [node_db]
    type=RocksDB
    path=db/hashnode
    open_files=1200
    filter_bits=12
    cache_mb=256
    file_size_mb=8
    file_size_mult=2

### Configuring your Node DB

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. See above for an example RocksDB configuration.

-   **Note**: HyperLevelDB and RocksDB are not available on Windows platform.


-----------------------------------------------------------

rippled Version 0.19
============================

rippled version 0.19 has now been released. This release is currently the tip of the [release](https://github.com/ripple/rippled/tree/release) branch and the tag is [0.19.0](https://github.com/ripple/rippled/tree/0.19.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit 26783607157a8b96e6e754f71565f4eb0134efc1
    Author: Vinnie Falco <vinnie.falco@gmail.com>
    Date:   Fri Nov 22 23:36:50 2013 -0800

        Set version to 0.19.0

**Significant Changes**

-   Bugfixes and improvements in path finding, path filtering, and payment execution.
-   Updates to HyperLevelDB and LevelDB node storage back ends.
-   Addition of RocksDB node storage back end.
-   New resource manager for tracking server load.
-   Fixes for a few bugs that can crashes or inability to serve client requests.

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file (or place this in your config file):

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**New RocksDB back end**

RocksDB is based on LevelDB with improvements from Facebook and the community. Preliminary tests show that it stall less often than HyperLevelDB.

If you are switching over from an existing back end, you have two choices. You can remove your old database or you can import it.

To remove your old database, make sure the server is shutdown. Remove the `db/ledger.db` and `db/transaction.db` files. Remove all the files in your back end store directory, `db/hashnode` by default. Then you can change your configuration file to use the RocksDB back end and restart.

To import your old database, start by shutting the server down. Then modify the configuration file by renaming your `[node_db]` portion to `[import_db]`. Create a new `[node_db]` section specify a RocksDB back end and a different directory. Start the server with `rippled --import`. When the import finishes, stop the server (it can take several minutes to shut down after an import), remove the old database, put the new database into place, remove the `[import_db]` section, change the `[node_db]` section to refer to the final location, and restart the server.

The recommended RocksDB configuration is:

    [node_db]
    type=RocksDB
    path=db/hashnode
    open_files=1200
    filter_bits=12
    cache_mb=256
    file_size_mb=8
    file_size_mult=2

**Configuring your Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. See above for an example RocksDB configuration.

-   **Note:** HyperLevelDB and RocksDB are not available on Windows platform.


-----------------------------------------------------------

rippled Version 0.16
============================

rippled version 0.16 has now been released. This release is currently the tip of the [master](https://github.com/ripple/rippled/tree/master) branch and the tag is [v0.16.0](https://github.com/ripple/rippled/tree/v0.16.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit 15ef43505473225af21bb7b575fb0b628d5e7f73
    Author: vinniefalco
    Date:   Wed Oct 2 2013

       Set version to 0.16.0

**Significant Changes**

-   Improved peer discovery
-   Improved pathfinding
-   Ledger speed improvements
-   Reduced memory consumption
-   Improved server stability
-   rippled no longer throws and exception on exiting
-   Better error reporting
-   Ripple-lib tests have been ported to use the Mocha testing framework

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file:

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. In most cases, that will mean adding this to your configuration file:

    [node_db]
    type=HyperLevelDB
    path=db/hashnode

-   NOTE HyperLevelDB is not available on Windows platforms.

**Release Candidates**

**Issues**

None known


-----------------------------------------------------------

rippled Version 0.14
============================

rippled version 0.14 has now been released. This release is currently the tip of the [master](https://github.com/ripple/rippled/tree/master) branch and the tag is [v0.12.0](https://github.com/ripple/rippled/tree/v0.14.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit b6d11c08d0245ee9bafbb97143f5d685dd2979fc
    Author: vinniefalco
    Date:   Wed Oct 2 2013

       Set version to 0.14.0

**Significant Changes**

-   Improved peer discovery
-   Improved pathfinding
-   Ledger speed improvements
-   Reduced memory consumption
-   Improved server stability
-   rippled no longer throws and exception on exiting
-   Better error reporting
-   Ripple-lib tests have been ported to use the Mocha testing framework

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file:

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. In most cases, that will mean adding this to your configuration file:

    [node_db]
    type=HyperLevelDB
    path=db/hashnode

-   NOTE HyperLevelDB is not available on Windows platforms.

**Release Candidates**

**Issues**

None known


-----------------------------------------------------------

rippled Version 0.12
============================

rippled version 0.12 has now been released. This release is currently the tip of the [master branch](https://github.com/ripple/rippled/tree/master) and can be found on GitHub. The tag is [v0.12.0](https://github.com/ripple/rippled/tree/v0.12.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit d0a9da6f16f4083993e4b6c5728777ffebf80f3a
    Author: JoelKatz <DavidJoelSchwartz@GMail.com>
    Date:   Mon Aug 26 12:08:05 2013 -0700

        Set version to v0.12.0

**Major Partner Issues Fixed**

-   Server Showing "Offline"

This issue was caused by LevelDB periodically compacting its internal data structure. While compacting, rippled's processing would stall causing the node to lose sync with the rest of the network. This issue was solved by switching from LevelDB to HyperLevelDB. rippled operators will need to change their ripple.cfg file. See below for configuration details.

-   Premature Validation of Transactions

On rare occasions, a transaction would show as locally validated before the full network consensus was confirmed. This issue was resolved by changing the way transactions are saved.

-   Missing Ledgers

Occasionally, some rippled servers would fail to fetch all ledgers. This left gaps in the local history and caused some API calls to report incomplete results. The ledger fetch code was rewritten to both prevent this and to repair any existing gaps.

**Significant Changes**

-   The way transactions are saved has been changed. This fixes a number of ways transactions can incorrectly be reported as fully-validated.
-   `doTransactionEntry` now works against open ledgers.
-   `doLedgerEntry` now supports a binary option.
-   A bug in `getBookPage` that caused it to skip offers is fixed.
-   `getNodeFat` now returns deeper chains, reducing ledger acquire latency.
-   Catching up if the (published ledger stream falls behind the network) is now more aggressive.
-   I/O stalls are drastically reduced by using the HyperLevelDB node back end.
-   Persistent ledger gaps should no longer occur.
-   Clusters now exchange load information.

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file:

<strike>

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

</strike>

**Update April 2014** - Due to a vulnerability in OpenSSL the validator keys above have been cycled out, the five validators by RippleLabs use the following keys now:

    [validators]
    n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7     RL1
    n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj     RL2
    n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C     RL3
    n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS     RL4
    n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA     RL5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. In most cases, that will mean adding this to your configuration file:

    [node_db]
    type=HyperLevelDB
    path=db/hashnode

-   NOTE HyperLevelDB is not available on Windows platforms.

**Release Candidates**

RC1 was the first release candidate.

RC2 fixed a bug that could cause ledger acquires to stall.

RC3 fixed compilation under OSX.

RC4 includes performance improvements in countAccountTx and numerous small fixes to ledger acquisition.

RC5 changed the peer low water mark from 4 to 10 to acquire more server connections.

RC6 fixed some possible load issues with the network state timer and cluster reporting timers.

**Issues**

Fetching of historical ledgers is slower in this build than in previous builds. This is being investigated.
