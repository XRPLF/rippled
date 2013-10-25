################################### REQUIRES ###################################

# This gives coffee-script proper file/lines in the exceptions

async       = require("async")
assert      = require 'assert'
Amount      = require("ripple-lib").Amount
Remote      = require("ripple-lib").Remote
Seed        = require("ripple-lib").Seed
Base        = require("ripple-lib").Base
Transaction = require("ripple-lib").Transaction
sjcl        = require("ripple-lib").sjcl
Server      = require("./server").Server
testutils   = require("./testutils")

#################################### HELPERS ###################################

pretty_json =  (v) -> JSON.stringify(v, undefined, 2)

exports.TestAccount = class TestAccount
  SHA256_RIPEMD160: (bits) ->
    sjcl.hash.ripemd160.hash sjcl.hash.sha256.hash(bits)

  get_address:  (passphrase) ->
    key_pair = Seed.from_json(passphrase).get_key()
    pubKey = sjcl.codec.hex.toBits key_pair.to_hex_pub()
    Base.encode_check(0,sjcl.codec.bytes.fromBits(@SHA256_RIPEMD160 pubKey))

  constructor: (passphrase) ->
    @passphrase = passphrase
    @address    = @get_address(passphrase)

############################# LEDGER STATE COMPILER ############################

exports.LedgerState = class LedgerState
  setup_issuer_realiaser: ->
    users = @config.accounts
    lookup = {}
    accounts = []

    for name, user of users
      accounts.push user.account
      lookup[user.account] = name

    realias = new RegExp(accounts.join("|"), "g")
    @realias_issuer = (str) -> str.replace(realias, (match) ->lookup[match])

  parse_amount: (amt_val) ->
    try
      amt = Amount.from_json(amt_val)
      if not amt.is_valid()
        throw new Error()
    catch e
      try
        amt = Amount.from_human(amt_val)
        if not amt.is_valid()
          throw new Error()
      catch e
        amt = null
    amt

  amount_key: (amt) ->
    currency = amt.currency().to_json()
    issuer = @realias_issuer amt.issuer().to_json()
    key = "#{currency}/#{issuer}"
    key.issuer = issuer
    key

  apply: (context)->
    @create_accounts_by_issuing_xrp_from_root(context)
    @create_trust_limits(context)
    @deliver_ious(context)

  record_iou: (account_id, amt)->
    key = @amount_key amt
    @assert @declaration.accounts[key.split('/')[1]]?,
           "Account for #{key} does not exist"

    a_ious = @ensure account_id, @ious
    @assert !a_ious[key]?,
           "Account #{account_id} has more than one amount for #{key}"
    a_ious[key] = amt

  ensure: (account_id, obj, val) ->
    if not obj[account_id]?
      obj[account_id] = val ? {}
    obj[account_id]

  record_xrp: (account_id, amt)->
    @assert !@accounts[account_id]?,
           "Already declared XRP for #{account_id}"
    @accounts[account_id] = amt

  record_trust: (account_id, amt, is_balance) ->
    key = @amount_key amt
    a_trusts = @ensure account_id, @trusts_by_ci

    if a_trusts[key]? and !is_balance
      cmp = amt.compareTo a_trusts[key]
      @assert cmp != - 1,
             "Account #{account_id} trust is less than balance for #{key}"
    a_trusts[key] = amt

  compile_explicit_trusts: ->
    for account_id, account of @declaration.accounts
      if not account.trusts?
        continue

      for amt_val in account.trusts
        amt = @parse_amount amt_val
        @assert amt != null and !amt.is_native(),
               "Trust amount #{amt_val} specified for #{account_id} is not valid"
        @record_trust(account_id, amt, false)

  compile_accounts_balances_and_implicit_trusts: ->
    for account_id, account of @declaration.accounts
      xrp_balance = null

      @assert account.balance?,
             "No balance declared for #{account_id}"

      for amt_val in account.balance
        amt = @parse_amount amt_val
        @assert amt != null,
               "Balance amount #{amt_val} specified for #{account_id} is not valid"

        if amt.is_native()
          xrp_balance = @record_xrp(account_id, amt)
        else
          @record_iou(account_id, amt)
          @record_trust(account_id, amt, true)

      @assert xrp_balance,
             "No XRP balanced declared for #{account_id}"

  compile_offers: ->
    for account_id, account of @declaration.accounts
      if not account.offers?
        continue
      for offer in account.offers
        [pays, gets, splat...] = offer
        gets_amt = @parse_amount gets
        @assert gets_amt != null,
               "For account #{account_id} taker_gets amount #{gets} is invalid"

        pays_amt = @parse_amount pays
        @assert pays_amt != null,
               "For account #{account_id} taker_pays amount #{pays} is invalid"

        a_offers = @ensure(account_id, @offers_by_ci)
        a_offers = @ensure(account_id, @offers_by_ci)
        offers_all = @ensure('offers', a_offers, [])

        if gets_amt.is_native()
          total = a_offers.xrp_total ?= new Amount.from_json('0')
          new_total = total.add(gets_amt)
          @assert @accounts[account_id].compareTo(new_total) != - 1,
            "Account #{account_id}s doesn't have enough xrp to place #{offer}"
        else
          key = @amount_key gets_amt
          key_offers = @ensure(key, a_offers, {})

          total = key_offers.total ?= Amount.from_json("0/#{key}")
          new_total = total.add(gets_amt)
          a_ious = @ensure(account_id, @ious)
          @assert a_ious[key]?,
                 "Account #{account_id} doesn't hold any #{key}"
          @assert a_ious[key].compareTo(new_total) != - 1,
                 "Account #{account_id} doesn't have enough #{key} to place #{offer}"

          key_offers.total = new_total

        offers_all.push [pays_amt, gets_amt, splat...]

    @offers = []
    for account_id, obj of @offers_by_ci
      for offer in obj.offers
        sliced = offer[0..]
        sliced.unshift account_id
        @offers.push sliced
      # @offers[account_id] = obj.offers

  base_reserve: ->
    @declaration.reserve?.base ? "50.0"

  incr_reserve: ->
    @declaration.reserve?.base ? "12.5"

  check_reserves: ->
    base_reserve_amt = @base_reserve()
    incr_reserve_amt = @incr_reserve()

    base_reserve = @parse_amount base_reserve_amt
    inc_reserve = @parse_amount incr_reserve_amt

    @assert base_reserve != null,
           "Base reserve amount #{base_reserve_amt} is invalid"

    @assert base_reserve != null,
           "incremental amount #{incr_reserve_amt} is invalid"

    for account_id, account of @declaration.accounts
      total_needed = base_reserve.clone()
      owner_count = 0

      a_offers = @offers_by_ci[account_id]
      if a_offers?
        if a_offers.xrp_total?
          total_needed = total_needed.add  a_offers.xrp_total
        if a_offers.offers?
          owner_count += @offers_by_ci[account_id].offers.length

      if @trusts_by_ci[account_id]?
        owner_count += Object.keys(@trusts_by_ci[account_id]).length

      owner_count_amount = Amount.from_json(String(owner_count))
      inc_reserve_n = owner_count_amount.multiply(inc_reserve)
      total_needed = total_needed.add(inc_reserve_n)

      @assert  @accounts[account_id].compareTo total_needed != - 1,
             "Account #{account_id} needs more XRP for reserve"

      @reserves[account_id] = total_needed

  format_payments: ->
    # We do these first as the following @ious need xrp to issue ious ;0
    for account_id, xrps of @accounts
      @xrp_payments.push ['root', account_id, xrps]

    for account_id, ious of @ious
      for curr_issuer, amt of ious
        src = @realias_issuer amt.issuer().to_json()
        dst = account_id
        @iou_payments.push [src, dst, amt]

  format_trusts: ->
    for account_id, trusts of @trusts_by_ci
      for curr_issuer, amt of trusts
        @trusts.push [account_id, amt]

  transactor: (fn, args_list, on_each, callback) ->
    if args_list.length == 0
      return callback()

    if not callback?
      callback = on_each
      on_each = null

    @assert callback?, "Must supply a callback"
    finalized = {
      n: args_list.length
      one: ->
        if --finalized.n <= 0
          callback()
    }

    async.concatSeries(args_list, ((args, callback) =>
      tx = @remote.transaction()
      on_each?(args..., tx)
      fn.apply(tx, args).on("proposed", (m) =>
        @assert m.engine_result is "tesSUCCESS", "Transactor failure: #{pretty_json m}"
        callback()
      ).on('final', (m) =>
        finalized.one()
      )
      .on("error", (m) =>
         assert false, pretty_json m
      ).submit()
    ),
      => testutils.ledger_close @remote, ->
    )

  requester: (fn, args_list, on_each, callback) ->
    if not callback?
      callback = on_each
      on_each = null

    @assert callback?, "Must supply a callback"

    async.concatSeries(args_list, ((args, callback) =>
      req = fn.apply @remote, args
      on_each?(args..., req)
      req.on("success", (m) =>
        if m.status?
          @assert m.status is "success", "requester failure: #{pretty_json m}"
        callback()
      ).on("error", (m) =>
         @assert false, pretty_json m
      ).request()
    ), -> callback())

  ensure_config_has_test_accounts: ->
    for account of @declaration.accounts
      if not @config.accounts[account]?
        acc = @config.accounts[account] = {}
        user = new TestAccount(account)
        acc.account = user.address
        acc.secret = user.passphrase
        # Index by nickname ...
        @remote.set_secret account, acc.secret
        # ... and by account ID
        @remote.set_secret acc.account, acc.secret
    @setup_issuer_realiaser()

  pretty_json: (v) ->
    @realias_issuer pretty_json(v)

  constructor: (declaration, @assert, @remote, @config) ->
    @declaration = declaration
    @accounts = {} # {$account_id : $xrp_amt}
    @trusts_by_ci   = {} # {$account_id : {$currency/$issuer : $iou_amt}}
    @ious     = {} # {$account_id : {$currency/$issuer : $iou_amt}}
    @offers_by_ci   = {} # {$account_id : {offers: [], $currency/$issuer : {total: $iou_amt}}}
    @reserves = {}

    @xrp_payments = [] # {$account_id: []}
    @trusts = [] # {$account_id: []}
    @iou_payments = [] # {$account_id: []}
    @offers = [] # {$account_id: []}

    @ensure_config_has_test_accounts()
    @compile_accounts_balances_and_implicit_trusts()
    @compile_explicit_trusts()
    @compile_offers()
    @check_reserves()
    @format_payments()
    @format_trusts()

  setup: (log, done) ->
    LOG = (m) ->
      self.what = m
      log(m)

    accounts = (k for k,ac of @accounts).sort()
    @remote.set_account_seq(seq, 1) for seq in accounts.concat 'root'   # <--
    accounts_apply_arguments = ([ac] for ac in @accounts)
    self = this

    async.waterfall [
      (cb) ->
        self.transactor(
            Transaction::payment,
            self.xrp_payments,
            ((src, dest, amt) ->
               LOG("Account `#{src}` creating account `#{dest}` by 
                     making payment of #{amt.to_text_full()}") ),
            cb)
      (cb) ->
        self.transactor(
            Transaction::ripple_line_set,
            self.trusts,
            ((src, amt) ->
              issuer = self.realias_issuer amt.issuer().to_json()
              currency = amt.currency().to_json()
              LOG("Account `#{src}` trusts account `#{issuer}` for 
                    #{amt.to_text()} #{currency}") ),
            cb)
      (cb) ->
        self.transactor(
            Transaction::payment,
            self.iou_payments,
            ((src, dest, amt, tx) ->
               LOG("Account `#{src}` is making a payment of #{amt.to_text_full()}
                    to `#{dest}`") ),
            cb)
      (cb) ->
        self.transactor(
            Transaction::offer_create,
            self.offers,
            ((src, pays, gets) ->
               LOG("Account `#{src}` is selling #{gets.to_text_full()}
                     for #{pays.to_text_full()}")),
            cb)
      (cb) ->
        testutils.ledger_close self.remote, cb
      (cb) ->
        self.requester(Remote::request_account_lines, accounts_apply_arguments,
            ((acc) ->
               LOG("Checking account_lines for #{acc}")),
            cb)
      (cb) ->
        self.requester(Remote::request_account_info, accounts_apply_arguments,
            ((acc) ->
               LOG("Checking account_info for #{acc}")),
            cb)
    ], (error) ->
      assert !error,
             "There was an error @ #{self.what}"
      done()