##0.13.0 (release candidate)

+ [Fix: Emit error events and return error on pathfind](https://github.com/ripple/ripple-lib/commit/1ccbaf677631a1944eb05d90f7afc5f3690a03dd)
+ [Deprecate core and remove snake case method copying](https://github.com/ripple/ripple-lib/commit/fb8dc44ec1d49bb05cd0cdbe6dd4ab211195868a)
+ Add new RippleAPI interface
    - [RippleAPI README and samples](https://github.com/ripple/ripple-lib/tree/develop/docs/samples)
    - [Method documentation](https://rawgit.com/ripple/ripple-lib/develop/docs/api.html)
+ [Fix RangeSet for validated_ledger as single ledger](https://github.com/ripple/ripple-lib/commit/9f9e76f8b933201651af59307135f67cfa7d60e8)
+ [Fix bug where the paths would be set with an empty array](https://github.com/ripple/ripple-lib/commit/83874ec0962da311b76f2385623e51c68bc39035)
+ [Fix reserve calculation](https://github.com/ripple/ripple-lib/commit/52879febb92d876f01f2e4d70871baa07af631fb)

##0.12.6

+ [Fix webpack require failure due to "./" notation](https://github.com/ripple/ripple-lib/commit/8d9746d7b10be203ee613df523c2522012ff1baf)

##0.12.15

+ [Add offer autobridging](https://github.com/ripple/ripple-lib/commit/c7bbce83719c1e8c6a4fae5ca850e7515db1a4a5)

+ [Prevent crash when listening for "model" events on the OrderBook class](https://github.com/ripple/ripple-lib/commit/5824c3cb7cb6bd834d6e037f69943aebf3d83351)

+ [Fix empty order edgecase](https://github.com/ripple/ripple-lib/commit/64809d9ae23dc24f47accd4b4788b48f49880d3e)

+ [Fix AutobridgeCalculator (RT-3445)](https://github.com/ripple/ripple-lib/commit/1fff5ea6dcbcee856536df26f3b9cf1aec3c3b55)

+ [Update sjcl and delete custom ripemd160, montgomery, and jacobi](https://github.com/ripple/ripple-lib/commit/50cda426eb83599c38c0b725e1524a01fc415da2)

+ [Fix transaction summary for transactions that fail with remoteError](https://github.com/ripple/ripple-lib/commit/5e714f6143464d7912f42537acaa553b88eaf6dc)

+ [Fix serializedobject append for excessively large bytes length](https://github.com/ripple/ripple-lib/commit/e93f1ab6f4aaad347450aee75a169af0faa2121c)

+ [Switch to sjcl npm module](https://github.com/ripple/ripple-lib/commit/9a502580fd89ec6a9aa55f4e5847f6a4a2cb5bba)

+ [Add babel transpiler](https://github.com/ripple/ripple-lib/commit/398f8d001f758bf575b959537a17e79e4042d17b)

+ [Remove unused float.js and wallet.js](https://github.com/ripple/ripple-lib/commit/d4a4b5f4fbbf09677a59ce81bace35c6426a2fda)

+ [Remove config singleton to reduce global state](https://github.com/ripple/ripple-lib/commit/c655c2a20ee5d150a4b5a1b6717b9fb81f636025)

##0.12.4

+ [Improve entropy security](https://github.com/ripple/ripple-lib/commit/c7ba822320880037796f57876d1abb4e525648ed)

+ [Remove unused crypt.js file](https://github.com/ripple/ripple-lib/commit/1f68eba1461bca03a4d22872450d15ae5a185334)

##0.12.3

+ [Add getLedgerSequence to Remote](https://github.com/ripple/ripple-lib/commit/d09548d04d3238fca653d482ec1d5faa7254559a)

+ [Improve randomness when generating ECDSA signatures](https://github.com/ripple/ripple-lib/commit/fe7e30b737ead6e71adfa466f5835ba546feab31)

+ [Improve SerializedObject.append performance](https://github.com/ripple/ripple-lib/commit/f7c35b118ebba549a64bcaa1a0629385ec6dbf6f)

+ [Add `Amount.scale`. Multiply an amount’s value by a scale factor](https://github.com/ripple/ripple-lib/commit/74dac97b368493056474468520f05671f458a69f)


##0.12.2

+ [Check that stack trace is available, fixes logging in browser](https://github.com/ripple/ripple-lib/commit/53cae3a66d48e88e8a6bbb96d6489ce7b9e22975)


##0.12.1

**Breaking Changes**

+ [Removed support for parsing native amounts in floating point format](https://github.com/ripple/ripple-lib/commit/e80cd1ff55deae9cd5b0ae85be957f86856b887e)


**Changes**

+ [Fix taker pays funded calculation](https://github.com/ripple/ripple-lib/commit/5af824f5cf46c7b9caa58ee0a757bf854d26c8dc)

+ [Fix order funded amount calculation](https://github.com/ripple/ripple-lib/commit/b2cdb1a6aed968b1f306e8dadbd4b7ca37e5aa03)

+ [Fix handling of quality in order book](https://github.com/ripple/ripple-lib/commit/2a5a8b498da60df738ba18d5c265f34771e8a1af)

+ [Fix currency parsing of non-alphanumeric and no-currency currencies](https://github.com/ripple/ripple-lib/commit/2166bb2e88eae8d5f1aba77338f69e8a9edf6a6f)

+ [Add Amount.strict_mode for toggling range validation](https://github.com/ripple/ripple-lib/commit/b5ed8f59a7dab1a17491618b8d9193646c314fb4)

+ [Add filename and line number to log, use log.warn() for deprecations](https://github.com/ripple/ripple-lib/commit/90329d3d73f1a76675063655b407513e32dc048b)

+ [Add GlobalFreeze and NoFreeze flags](https://github.com/ripple/ripple-lib/commit/e2ed2bdbf6f01c7d4d690c2cf0b83fba94558dd7)

+ [Fix handling of falsy parameters in requestLedger](https://github.com/ripple/ripple-lib/commit/6023efed41b7812b3bab660a1c0dc9f0a21000b9)

+ [Fix Base:decode](https://github.com/ripple/ripple-lib/commit/719f39c01c6941d9a650aa94f95617793dd53ea0)

+ [Fix Amount: clone in ratio_human, product_human](https://github.com/ripple/ripple-lib/commit/19e17a8431550cf156b1ad669a19dedfe4e28e4a)

+ [Fix Amount.to_human for very small numbers](https://github.com/ripple/ripple-lib/commit/6abfa759aa09d68074ac558d96c4b126a7cd1719)

+ [Refactor base conversion](https://github.com/ripple/ripple-lib/commit/f2b63fa4a80663eb29472bc6bb1aea8159f1f205)

+ [Update binary transaction format](https://github.com/ripple/ripple-lib/commit/8e134918fb4c22983320a3102f955e4568bb1dfb)

+ [Add DefaultRipple account flag](https://github.com/ripple/ripple-lib/commit/3e249902c4cf25b4da5e75048c84ae391be83b10)

+ [Remove `Features` field requirement in `SetFee` transaction format](https://github.com/ripple/ripple-lib/commit/a20a649013646710c078d4ce1e210f87c7fe74fe)

+ [Remove `RegularKey` field requirement in `SetRegularKey` transaction format](https://github.com/ripple/ripple-lib/commit/c275174f27877ba8f389eb4efe969feb514d6e46)


##0.12.0

**Breaking Changes**

+ REMOVED Remote storage interface
+ REMOVED Remote `ping` configuration
+ REMOVED Old/deprecated Remote server configuration (websocket_ip, websocket_port)
+ REMOVED browser `online` reconnect listener
    - [Cleanup, deprecations - 2833a7b6](https://github.com/ripple/ripple-lib/commit/2833a7b66e696dab427464625077f9b93092d0d5)

+ Remove `jsbn` and use `bignumber.js` instead for big number math
+ The `allow_nan` flag has been removed. Results for invalid amounts will always be `NaN`
    - [Refactor to use bignumber.js - d025b4a0](https://github.com/ripple/ripple-lib/commit/d025b4a0c3a98a6de27a1bee9573c85347bcd66b)
    - [Handle invalid input in parse_human - c8f18c8c](https://github.com/ripple/ripple-lib/commit/c8f18c8c8590b7b48e370e0325b6677b7720294f)
    - [Check for null in isNumber - b86790c8](https://github.com/ripple/ripple-lib/commit/b86790c8543c239a532fd7697d4652829019d385)
    - [Cleanup amount.js - d0fb291c](https://github.com/ripple/ripple-lib/commit/d0fb291c4e330193a244902156f1d74730da357d)


**Changes**

+ [Add deprecation warnings to request constructors. The first argument to request constructor functions should be an object containing request properties](https://github.com/ripple/ripple-lib/commit/35d76b3520934285f80059c1badd6c522539104c)

+ [Fix taker_gets_funded exceeding offer.TakerGets](https://github.com/ripple/ripple-lib/commit/b19ecb4482b589d575382b7a5d0480b963383bb1)

+ [Fix unsymmetric memo serializing](https://github.com/ripple/ripple-lib/commit/1ed36fabdbd54f4d31078c2b0eaa3becc0fe2821)

+ [Fix IOU value passed to `Amount.from_json()`](https://github.com/ripple/ripple-lib/commit/fd1b64393dffb3d1819cd40b8d43df43a4db042d)

+ [Update transaction binary parsing to account for XRP delivered amounts](https://github.com/ripple/ripple-lib/commit/35a346a674e6ee1e1e495db93700d55984efc7dd)

+ [Bumped dependencies](https://github.com/ripple/ripple-lib/commit/f9bc7cc746b44b24b61bbe260ae2e9d9617286da)



##0.11.0

+ [Track the funded status of an order based on cumulative account orders](https://github.com/ripple/ripple-lib/commit/67d39737a4d5e0fcd9d9b47b9083ee00e5a9e652) and [67d3973](https://github.com/ripple/ripple-lib/commit/b6b99dde022e1e14c4797e454b1d7fca50e49482)

+ Remove blobvault client from ripple-lib, use the [`ripple-vault-client`](https://github.com/ripple/ripple-vault-client) instead [9b3d62b7](https://github.com/ripple/ripple-lib/commit/9b3d62b765c4c25beae6eb0fa57ef3a07f2581b1)

+ [Add support for `ledger` option in requestBookOffers](https://github.com/ripple/ripple-lib/commit/34c0677c453c409ef0a5b351959abdc176d3bacb)

+ [Add support for `limit` option in requestBookOffers](https://github.com/ripple/ripple-lib/commit/d1d4452217c878d0b377d24830b4cd8b3162f6e0)

+ [Add `ledgerSelect` request constructor in `Remote`](https://github.com/ripple/ripple-lib/commit/98f40abfc3aa74dec5067a2d90002756cc8acd01)

+ [Default to binary data for commands that accept the binary flag](https://github.com/ripple/ripple-lib/commit/7cb113fcbcfc1e3e9830a999148b3e78df3387cc)

+ [Fix metadata account check](https://github.com/ripple/ripple-lib/commit/3f61598d6c87e3cc877af60e2d515f9eff73dfe1)

+ [Double check `tes` code before emitting `success`](https://github.com/ripple/ripple-lib/commit/97a8c874903eb7309d8f755955ac80872f670582)

+ [Decrease redundancy in binary account_tx parsing](https://github.com/ripple/ripple-lib/commit/0aba638e6e7f4f6e22cb6424eed3897ebad90a5a)

+ [Abort server connection on unrecoverable TLS error](https://github.com/ripple/ripple-lib/commit/000a2ea00c57157044aeca0fb3f24b37669b163c)

+ [Fix complete ledgers check on subscription that is not initial](https://github.com/ripple/ripple-lib/commit/89de91301e682a46dc60aaacc7ae152e8fe1b7c7)


##0.10.0

+ [Transaction changes](https://github.com/ripple/ripple-lib/pull/221)

+ **Important** `tef*` and `tel*` and errors will no longer be presented as
final. Rather than considering these errors final, ripple-lib will wait until
the `LastLedgerSequence` specified in the transaction is exceeded.  This makes
failures more definitive, and ensures that no transaction will resubmit
indefinitely.

+ A new, final tej-class error is introduced to account for transactions that
are locally determined to have expired: `tejMaxLedger`.

+ [Allow per transaction fees to be set, `transaction.setFixedFee()`](https://github.com/ripple/ripple-lib/commit/9b22f279bcbe60ee6bcf4b7fa60a48e9c197a828)

+ [Improve memo support](https://github.com/ripple/ripple-lib/commit/1704ac4ae144c0ce54afad86f644c75a632080b1)
    - Add `MemoFormat` property for memo
    - Enforce `MemoFormat` and `MemoType` to be valid ASCII
    - Support `text` and `json` MemoFormat

+ [Update jscl library](https://github.com/ripple/ripple-lib/commit/3204998fcb6f31d6c90532a737a4adb8a1e420f6)
    - Improved entropy by taking advantage of platform crypto
    - Use jscl's k256 curve instead of altering the c256 curve with k256 configuration
    - **deprecated:** the c256 curve is linked to the k256 curve to provide backwards compatibility, this link will be removed in the future

+ [Fix empty queue check on reconnect](https://github.com/ripple/ripple-lib/commit/3c21994adcf72d1fbd87d453ceb917f9ad6df4ec)

##0.9.4

+ [Normalize offers from book_offers and transaction stream](https://github.com/ripple/ripple-lib/commit/86ed24b94cf7c8929c87db3a63e9bbea7f767e9c)

+ [Fix: Amount.to_human() precision rounding](https://github.com/ripple/ripple-lib/commit/e371cc2c3ceccb3c1cfdf18b98d80093147dd8b2)

+ [Fix: fractional drops in funded taker_pays setter](https://github.com/ripple/ripple-lib/commit/0d7fc0a573a144caac15dd13798b23eeb1f95fb4)

##0.9.3

+ [Change `presubmit` to emit immediately before transaction submit](https://github.com/ripple/ripple-lib/commit/7a1feaa89701bf861ab31ebd8ffdc8d8d1474e29)

+ [Add a "core" browser build of ripple-lib which has a subset of features and smaller file size](https://github.com/ripple/ripple-lib/pull/205)

+ [Update binformat with missing fields from rippled](https://github.com/ripple/ripple-lib/commit/cae980788efb00191bfd0988ed836d60cdf7a9a2)

+ [Wait for transaction validation before returning `tec` error](https://github.com/ripple/ripple-lib/commit/6bdd4b2670906588852fc4dda457607b4aac08e4)

+ [Change default `max_fee` on `Remote` to `1 XRP`](https://github.com/ripple/ripple-lib/commit/d6b1728c23ff85c3cc791bed6982a750641fd95f)

+ [Fix: Request ledger_accept should return the Remote](https://github.com/ripple/ripple-lib/pull/209)

##0.9.2

+ [**Breaking change**: Change accountRequest method signature](https://github.com/ripple/ripple-lib/commit/6f5d1104aa3eb440c518ec4f39e264fdce15fa15)

+ [Add paging behavior for account requests, `account_lines` and `account_offers`](https://github.com/ripple/ripple-lib/commit/722f4e175dbbf378e51b49142d0285f87acb22d7)

+ [Add max_fee setter to transactions to set max fee the submitter is willing to pay] (https://github.com/ripple/ripple-lib/commit/24587fab9c8ad3840d7aa345a7037b48839e09d7)

+ [Fix: cap IOU Amounts to their max and min value] (https://github.com/ripple/ripple-lib/commit/f05941fbc46fdb7c6fe7ad72927af02d527ffeed)

Example on how to use paging with `account_offers`:
```
// A valid `ledger_index` or `ledger_hash` is required to provide a reliable result.
// Results can change between ledger closes, so the provided ledger will be used as base.
var options = {
    account: < rippleAccount >,
    limit: < Number between 10 and 400 >,
    ledger: < valid ledger_index or ledger_hash >
}

// The `marker` comes back in an account request if there are more results than are returned
// in the current response. The amount of results per response are determined by the `limit`.
if (marker) {
    options.marker = < marker >;
}

var request = remote.requestAccountOffers(options);
```

[Full working example](https://github.com/geertweening/ripple-lib-scripts/blob/master/account_offers_paging.js)


##0.9.1

+ Switch account requests to use ledgerSelect rather than ledgerChoose ([278df90](https://github.com/ripple/ripple-lib/commit/278df9025a20228de22379a53c76ca12d40fa591))

+ **Deprecated** setting `ident` and `account_index` on account requests ([278df90](https://github.com/ripple/ripple-lib/commit/278df9025a20228de22379a53c76ca12d40fa591))

+ Change initial account transaction sequence to 1 ([a3c1d06](https://github.com/ripple/ripple-lib/commit/a3c1d06eba883dc84fe2bfe700e4309795c84cac))

+ Fix: instance transaction withoute remote ([d3b6b81](https://github.com/ripple/ripple-lib/commit/d3b6b8127c7b01e416b400c25abf1719bdd008ca))

+ Fix: account root request ledger argument ([bc1f9f8](https://github.com/ripple/ripple-lib/commit/bc1f9f8a286b187d36ebaf552694e31e73742293))

+ Fix: rsign.js local signing and example ([d3b6b81](https://github.com/ripple/ripple-lib/commit/d3b6b8127c7b01e416b400c25abf1719bdd008ca) and [f1004c6](https://github.com/ripple/ripple-lib/commit/f1004c6db2a0ce59bbabbb8f2b355a9fd9995fd8))


##0.9.0

+ Add routes to the vault client for KYC attestations ([ed2da574](https://github.com/ripple/ripple-lib/commit/ed2da57475acf5e9d2cf3373858f4274832bd83f))

+ Currency: add `show_interest` flag to show or hide interest in `Currency.to_human()` and `Currency.to_json()` [Example use in tests](https://github.com/ripple/ripple-lib/blob/947ec3edc2e7c8f1ef097e496bf552c74366e749/test/currency-test.js#L123)

+ Configurable maxAttempts for transaction submission ([d107092](https://github.com/ripple/ripple-lib/commit/d10709254061e9e4416d2cb78b5cac1ec0d7ffa5))

+ Binformat: added missing TransactionResult options ([6abed8d](https://github.com/ripple/ripple-lib/commit/6abed8dd5311765b2eb70505dadbdf5121439ca8))

+ **Breaking change:** make maxLoops in seed.get_key optional. [Example use in tests](https://github.com/ripple/ripple-lib/blob/23e473b6886c457781949c825b3ff48b3984e51f/test/seed-test.js) ([23e473b](https://github.com/ripple/ripple-lib/commit/23e473b6886c457781949c825b3ff48b3984e51f))

+ Shrinkwrap packages for dependency locking ([2dcd5f9](2dcd5f94fbc71200eb08a5044c76ef94f7971913))

+ Fix: Amount.to_human() precision bugs ([4be209e](https://github.com/ripple/ripple-lib/commit/4be209e286b5b209bec7bcd1212098985e15ff2f) and [7708c64](https://github.com/ripple/ripple-lib/commit/7708c64576e70ce3ac190442daceb30e4446aab7))

+ Fix: change handling of requestLedger options ([57b7030](https://github.com/ripple/ripple-lib/commit/57b70300f5f0c7534ede118ddbb5d8762668a4f8))


##0.8.2

+ Currency: Allow mixed letters and numbers in currencies

+ Deprecate account_tx map/reduce/filterg

+ Fix: correct requestLedger arguments

+ Fix: missing subscription on error events for some server methods

+ Fix: orderbook reset on reconnect

+ Fix: ripple-lib crashing. Add potential missing error handlers


##0.8.1

+ Wallet: Add Wallet class that generates wallets

+ Make npm test runnable in Windows.

+ Fix several stability issues, see merged PR's for details

+ Fix bug in Amount.to_human_full()

+ Fix undefined fee states when connecting to a rippled that is syncing


##0.8.0

+ Orderbook: Added tracking of offer funds for determining when offers are not funded

+ Orderbook: Added tests

+ Orderbook: Update owner funds

+ Transactions: If transaction errs with `tefALREADY`, wait until all possible submissions err with the same before emitting `error`. Fixes a client "Transaction malformed" bug.

+ Transactions: Track submissions, don't bother submitting to unconnected servers

+ Request: `request.request()` now accepts an array of servers as first argument. Servers can be represented with URL, or the server object itself.

+ Request: `request.broadcast()` now returns the number of servers request was sent to

+ Server: Acquire host information from server without additional request

+ Amount: Add a constant for the maximum canonical value that can be expressed as a Ripple value

+ Amount: Make Constants static fields on the class, instead of a seperate export


##0.7.39

+ Improvements to multi-server support. Fixed an issue where a server's score was not reset and connections would keep dropping after being connected for a significant amount of time.

+ Improvements in order book support. Added support for currency pairs with interest bearing currencies. You can request an order book with hex, ISO code or full name for the currency.

+ Fix value parsing for amount/currency order pairs, e.g. `Amount.from_human("XAU 12345.6789")`

+ Improved Amount parsing from human readable string given a hex currency, e.g. `Amount.from_human("10 015841551A748AD2C1F76FF6ECB0CCCD00000000")`

+ Improvements to username normalization in the vault client

+ Add 2-factor authentication support for vault client

+ Removed vestiges of Grunt, switched to Gulp


##0.7.37

+ **Deprecations**

    1. Removed humanistic amount detection in `transaction.payment`. Passing `1XRP` as the payment amount no longer works.
    2. `remote.setServer` uses full server URL rather than hostname. Example: `remote.setServer('wss://s`.ripple.com:443')`
    3. Removed constructors for deprecated transaction types from `transaction.js`.
    4. Removed `invoiceID` option from `transaction.payment`. Instead, use the `transaction.invoiceID` method.
    5. Removed `transaction.transactionManager` getter.

+ Improved multi-server support. Servers are now ranked dynamically, and transactions are broadcasted to all connected servers.

+ Automatically ping connected servers. Client configuration now should contain `ping: <seconds>` to specify the ping interval.

+ Added `transaction.lastLedger` to specify `LastLedgerSequence`. Setting it this way also ensures that the sequence is not bumped on subsequent requests.

+ Added optional `remote.accountTx` binary parsing.
    ```js
      {
        binary: true,
        parseBinary: false
      }
    ```
+ Added full currency name support, e.g. `Currency.from_json('XRP').to_human({full_name:'Ripples'})` will return `XRP - Ripples`

+ Improved interest bearing currency support, e.g. `Currency.from_human('USD - US Dollar (2.5%pa)')`

+ Improve test coverage

+ Added blob vault client.  The vault client facilitates interaction with ripple's namespace and blob vault or 3rd party blob vaults using ripple's blob vault software (https://github.com/ripple/ripple-blobvault). A list of the available functions can be found at [docs/VAULTCLIENT.md](docs/VAULTCLIENT.md)


##0.7.35

+ `LastLedgerSequence` is set by default on outgoing transactions. This refers to the last valid ledger index (AKA sequence) for a transaction. By default, this index is set to the current index (at submission time) plus 8. In theory, this allows ripple-lib to deterministically fail a transaction whose submission request timed out, but whose associated server continues to emit ledger_closed events.

+ Transactions that err with `telINSUF_FEE_P` will be automatically resubmitted. This error indicates that the `Fee` supplied in the transaction submission request was inadquate. Ideally, the `Fee` is tracked by ripple-lib in real-time, and the resubmitted transaction will most likely succeed.

+ Added Transaction.iff(function(callback) { }). Callback expects first argument to be an Error or null, second argument is a boolean which indicates whether or not to proceed with the transaction submission. If an `iff` function is specified, it will be executed prior to every submission of the transaction (including resubmissions).

+ Transactions will now emit `presubmit` and `postsubmit` events. They will be emitted before and after a transaction is submitted, respectively.

+ Added Transaction.summary(). Returns a summary of a transaction in semi-human-readable form. JSON-stringifiable.

+ Remote.requestAccountTx() with `binary: true` will automatically parse transactions.

+ Added Remote.requestAccountTx filter, map, and reduce.

```js
  remote.requestAccountTx({
    account: 'retc',
    ledger_index_min: -1,
    ledger_index_max: -1,
    limit: 100,
    binary: true,

    filter: function(transaction) {
      return transaction.tx.TransactionType === 'Payment';
    },

    map: function(transaction) {
      return Number(transaction.tx.Amount);
    },

    reduce: function(a, b) {
      return a + b;
    },

    pluck: 'transactions'
  }, console.log)
```

+ Added persistence hooks.

+ General performance improvements, especially for long-running processes.

