################################### REQUIRES ###################################

# This gives coffee-script proper file/lines in the exceptions

async       = require 'async'
assert      = require 'assert'
{
  Amount
  Remote
  Seed
  Base
  UInt160
  Transaction
}           = require 'ripple-lib'
testutils   = require './testutils'

#################################### HELPERS ###################################

pretty_json =  (v) -> JSON.stringify(v, undefined, 2)

exports.TestAccount = class TestAccount
  SHA256_RIPEMD160: (bits) ->
    sjcl.hash.ripemd160.hash sjcl.hash.sha256.hash(bits)

  derive_pair:  (passphrase) ->
    seed = Seed.from_json(passphrase)
    master_seed = seed.to_json()
    key_pair = seed.get_key()
    pubKey = key_pair.pubKeyHex()
    address = key_pair.accountID()
    [address, master_seed, key_pair]

  constructor: (passphrase) ->
    @passphrase                         = passphrase
    [@address, @master_seed, @key_pair] = @derive_pair(passphrase)

parse_balance_and_trust = (val) ->
  reg = new RegExp("([0-9.]+)-([0-9.]+)(/[^/]+/[^/]+)")
  m = reg.exec val
  if m != null
    [m[1] + m[3], m[2] + m[3]]
  else
    undefined

exports.parse_amount = parse_amount = (amt_val) ->
  amt = Amount.from_json(amt_val)
  if not amt.is_valid()
      amt = Amount.from_human(amt_val)
      if not amt.is_valid()
        amt = null
  amt

exports.Balance = class Balance
  '''
  
  Represents a parsed balance declaration, which could represent an xrp balance
  or an iou balance and optional limit.
  
  @amount
  @limit
  @balance

  '''
  constructor: (value) ->
    limited = parse_balance_and_trust value
    if limited?
      [amount, limit] = limited
      @amount    = parse_amount amount
      @limit     = parse_amount limit
      @is_native = false
    else
      @amount    = parse_amount value
      @is_native = @amount.is_native()
      @limit     = null

################################################################################

class BulkRequests
  constructor: (@remote, @assert, @pretty_json) ->

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

    #remote = @remote
    async.concatSeries(args_list, ((args, callback) =>
      tx = @remote.transaction()
      fn.apply(tx, args)
      on_each?(args..., tx) # after payment() offer_create() etc so set_flags works

      tx.on("proposed", (m) =>
        @assert m.engine_result is "tesSUCCESS", "Transactor failure: #{@pretty_json m}"
        callback()
        # testutils.ledger_close remote, ->
      ).on('final', (m) =>
        finalized.one()
        # callback()
      )
      .on("error", (m) =>
         @assert false, @pretty_json m
      ).submit()
    ),
      => testutils.ledger_close @remote, ->
    )

  requester: (fn, args_list, on_each, callback, on_results) ->
    if not callback?
      callback = on_each
      on_each = null

    @assert callback?, "Must supply a callback"

    async.concatSeries(args_list, ((args, callback) =>
      req = fn.apply @remote, (args.map (arg) -> return { account: arg })
      on_each?(args..., req)
      req.on("success", (m) =>
        if m.status?
          @assert m.status is "success", "requester failure: #{@pretty_json m}"
        callback(null, m)
      ).on("error", (m) =>
         @assert false, @pretty_json m
      ).request()
    ),
      (error, results_list) ->
        on_results?(results_list)
        callback(error, results_list)
    )


################################# ALIAS MANAGER ################################

class AliasManager
  constructor: (@config, remote, aliases) ->
    '''

    @config
      includes `accounts` property, with structure same as that exported
      in testconfig.js

    @remote
      a Remote object
    
    @aliases
      A list of aliases

    '''
    @add_accounts_to_config(@config, aliases)
    @set_test_account_secrets(remote, @config)
    @realias_issuer = @create_issuer_realiaser()
    @alias_lookup = @create_alias_lookup()

  create_alias_lookup: ->
    lookup = {}
    for nick,acc of @config.accounts
      lookup[acc.account] = nick
    lookup

  lookup_alias: (address) ->
    @alias_lookup[UInt160.json_rewrite address]

  pretty_json: (v) =>
    @realias_issuer pretty_json(v)

  add_accounts_to_config: (config, accounts) ->
    for account in accounts
      if not config.accounts[account]?
        acc = config.accounts[account] = {}
        user = new TestAccount(account)
        acc.account = user.address
        acc.secret = user.master_seed

  set_test_account_secrets: (remote, config) ->
    # TODO: config.accounts
    for nick,acc of config.accounts
      # # Index by nickname ...
      remote.set_secret nick, acc.secret
      # # ... and by account ID
      remote.set_secret acc.account, acc.secret

  amount_key: (amt) ->
    currency = amt.currency().to_json()
    issuer = @realias_issuer amt.issuer().to_json()
    "#{currency}/#{issuer}"

  create_issuer_realiaser: ->
    users = @config.accounts
    lookup = {}
    accounts = []

    for name, user of users
      accounts.push user.account
      lookup[user.account] = name

    realias = new RegExp(accounts.join("|"), "g")
    (str) -> str.replace(realias, (match) ->lookup[match])

############################# LEDGER STATE COMPILER ############################

exports.LedgerState = class LedgerState
  parse_amount: (amt_val) ->
    parse_amount(amt_val)

  amount_key: (amt) ->
    @am.amount_key amt

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

    undefined

  compile_accounts_balances_and_implicit_trusts: ->
    for account_id, account of @declaration.accounts
      xrp_balance = null

      @assert account.balance?,
             "No balance declared for #{account_id}"

      for amt_val in account.balance
        trust = null
        balance_trust = parse_balance_and_trust(amt_val)

        if balance_trust?
          [amt_val, trust_val] = balance_trust
          trust = @parse_amount trust_val
          @assert trust != null,
                 "Trust amount #{trust_val} specified for #{account_id} "
                 "is not valid"

        amt = @parse_amount amt_val
        @assert amt != null,
               "Balance amount #{amt_val} specified for #{account_id} "
               "is not valid"

        if amt.is_native()
          xrp_balance = @record_xrp(account_id, amt)
        else
          @record_iou(account_id, amt)
          @record_trust(account_id, trust ? amt, true)

      @assert xrp_balance,
             "No XRP balanced declared for #{account_id}"

    undefined

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

        offers = @ensure(account_id, @offers_by_ci)
        offers = @ensure(account_id, @offers_by_ci)
        offers_all = @ensure('offers', offers, [])

        if gets_amt.is_native()
          total = offers.xrp_total ?= new Amount.from_json('0')
          new_total = total.add(gets_amt)
          @assert @accounts[account_id].compareTo(new_total) != - 1,
            "Account #{account_id}s doesn't have enough xrp to place #{offer}"
        else
          key = @amount_key gets_amt

          if key.split('/')[1] != account_id
            key_offers = @ensure(key, offers, {})

            total = key_offers.total ?= Amount.from_json("0/#{key}")
            new_total = total.add(gets_amt)
            a_ious = @ensure(account_id, @ious)
            @assert a_ious[key]?,
                   "Account #{account_id} doesn't hold any #{key}"
            @assert a_ious[key].compareTo(new_total) != - 1,
                   "Account #{account_id} doesn't have enough #{key}"
                   " to place #{offer}"

            key_offers.total = new_total

        offers_all.push [pays_amt, gets_amt, splat...]

    @offers = []
    for account_id, obj of @offers_by_ci
      for offer in obj.offers
        sliced = offer[0..]
        sliced.unshift account_id
        @offers.push sliced
      # @offers[account_id] = obj.offers

    @offers

  base_reserve: ->
    @declaration.reserve?.base ? "200.0"

  incr_reserve: ->
    @declaration.reserve?.base ? "50.0"

  check_reserves: ->
    base_reserve_amt = @base_reserve()
    incr_reserve_amt = @incr_reserve()

    base_reserve = @parse_amount base_reserve_amt
    incr_reserve = @parse_amount incr_reserve_amt

    @assert base_reserve != null,
           "Base reserve amount #{base_reserve_amt} is invalid"

    @assert incr_reserve != null,
           "incremental amount #{incr_reserve_amt} is invalid"

    for account_id, account of @declaration.accounts
      total_needed = base_reserve.clone()
      owner_count = 0

      offers = @offers_by_ci[account_id]
      if offers?
        if offers.xrp_total?
          total_needed = total_needed.add  offers.xrp_total
        if offers.offers?
          owner_count += @offers_by_ci[account_id].offers.length

      if @trusts_by_ci[account_id]?
        owner_count += Object.keys(@trusts_by_ci[account_id]).length

      owner_count_amount = Amount.from_json(String(owner_count))
      inc_reserve_n = owner_count_amount.multiply(incr_reserve)
      total_needed = total_needed.add(inc_reserve_n)

      @assert @accounts[account_id].compareTo(total_needed) != - 1,
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

    undefined

  format_trusts: ->
    for account_id, trusts of @trusts_by_ci
      for curr_issuer, amt of trusts
        @trusts.push [account_id, amt]

    undefined

  setup_alias_manager: ->
    @am = new AliasManager(@config, @remote, Object.keys(@declaration.accounts))
    @realias_issuer = @am.realias_issuer

  pretty_json: (v) =>
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

    @setup_alias_manager()
    @compile_accounts_balances_and_implicit_trusts()
    @compile_explicit_trusts()
    @compile_offers()
    @check_reserves()
    @format_payments()
    @format_trusts()
    @add_transaction_fees()

  compile_to_rpc_commands: ->
    passphrase = (src) ->
      if src == 'root'
        'masterpassphrase'
      else
        src

    make_tx_json = (src, tt) ->
      {"Account": UInt160.json_rewrite(src), "TransactionType": tt}

    submit_line = (src, tx_json) ->
      "build/rippled submit #{passphrase(src)} '#{JSON.stringify tx_json}'"

    lines = []
    ledger_accept = -> lines.push('build/rippled ledger_accept')

    for [src, dst, amount] in @xrp_payments
      tx_json = make_tx_json(src, 'Payment')
      tx_json.Destination =  UInt160.json_rewrite dst
      tx_json.Amount = amount.to_json()
      lines.push submit_line(src, tx_json)

    ledger_accept()

    for [src, limit] in @trusts
      tx_json = make_tx_json(src, 'TrustSet')
      tx_json.LimitAmount = limit.to_json()
      lines.push submit_line(src, tx_json)

    ledger_accept()

    for [src, dst, amount] in @iou_payments
      tx_json = make_tx_json(src, 'Payment')
      tx_json.Destination = UInt160.json_rewrite dst
      tx_json.Amount = amount.to_json()
      lines.push submit_line(src, tx_json)

    ledger_accept()

    for [src, pays, gets, flags] in @offers
      tx = new Transaction({secrets: {}})
      tx.offer_create(src, pays, gets)
      tx.set_flags(flags)

      lines.push submit_line(src, tx.tx_json)

    ledger_accept()
    lines.join('\n')

  verifier: (decl) ->
    new LedgerVerifier(decl ? @declaration, @remote, @config, @assert, @am)

  add_transaction_fees: ->
    extra_fees = {}
    account_sets = ([k] for k,ac of @accounts)
    fee = Amount.from_json(@remote.fee_cushion * 10)
    for list in [@trusts, @iou_payments, @offers, account_sets]
      for [src, args...] in list
        extra = extra_fees[src]
        extra = if extra? then extra.add(fee) else fee
        extra_fees[src] = extra

    for [src, dst, amount], ix in @xrp_payments
      if extra_fees[dst]?
        @xrp_payments[ix][2] = amount.add(extra_fees[dst])

  setup: (log, done) ->
    LOG = (m) ->
      self.what = m
      log(m)

    accounts = (k for k,ac of @accounts).sort()
    @remote.set_account_seq(seq, 1) for seq in accounts.concat 'root'   # <--
    accounts_apply_arguments = ([ac] for ac, _ of @accounts)
    self = this

    Dump = (v) => console.log @pretty_json(v)
    Dump = ->

    reqs = new BulkRequests(@remote, @assert, @pretty_json)

    async.waterfall [
      (cb) ->
        reqs.transactor(
            Transaction::payment,
            self.xrp_payments,
            ((src, dest, amt) ->
               LOG("Account `#{src}` creating account `#{dest}` by "+
                     "making payment of #{amt.to_text_full()}") ),
            cb)
      (cb) ->
        reqs.transactor(
          Transaction::account_set,
          accounts_apply_arguments,
          ((account, tx) ->
            tx.tx_json.SetFlag = 8
          ), cb)
      (cb) ->
        reqs.transactor(
            Transaction::ripple_line_set,
            self.trusts,
            ((src, amt) ->
              issuer = self.realias_issuer amt.issuer().to_json()
              currency = amt.currency().to_json()
              LOG("Account `#{src}` trusts account `#{issuer}` for "+
                    "#{amt.to_text()} #{currency}") ),
            cb)
      (cb) ->
        reqs.transactor(
            Transaction::payment,
            self.iou_payments,
            ((src, dest, amt, tx) ->
               LOG("Account `#{src}` is making a payment of #{amt.to_text_full()} "+
                    "to `#{dest}`") ),
            cb)
      (cb) ->
        reqs.transactor(
            Transaction::offer_create,
            self.offers,
            ((src, pays, gets, flags, tx) ->
              if not tx?
                tx = flags
                flags = ['Passive']
              else
                # TODO: icky ;)
                delete tx.tx_json.Expiration

              tx.set_flags(flags)
              LOG("Account `#{src}` is selling #{gets.to_text_full()} "+
                    "for #{pays.to_text_full()}")),
            cb)
      (cb) ->
        testutils.ledger_close self.remote, cb
    ], (error) ->
      assert !error,
             "There was an error @ #{self.what}"
      done()

################################ LEDGER VERIFIER ###############################

ensure = (account_id, obj, val) ->
  if not obj[account_id]?
    obj[account_id] = val ? {}
  obj[account_id]

exports.LedgerVerifier = class LedgerVerifier
  constructor: (@declaration, @remote, @config, @assert, @am) ->
    @am ?= new AliasManager(@config, @remote, Object.keys(@declaration.accounts))
    @requester = new BulkRequests(@remote, @assert, @am.pretty_json)
    @compile_declaration()

  verify_lines: (errors, account_lines) ->
    for account in account_lines
      # For test sweet ;)
      account_alias = @am.lookup_alias account.account
      for line in account.lines
        peer_alias = @am.lookup_alias line.account
        key = "#{line.currency}/#{peer_alias}"

        asserted = @iou_balances[account_alias]?[key]
        if asserted?
          actual = Amount.from_json(
              "#{line.balance}/#{line.currency}/#{line.account}")

          if not asserted.equals(actual)
            balance = (((errors[account_alias] ?= {})['balance'] ?= {}))
            balance[key] =
              expected: asserted.to_text()
              actual: actual.to_text()

        asserted = @trusts[account_alias]?[key]
        if asserted?
          actual = Amount.from_json(
              "#{line.limit}/#{line.currency}/#{line.account}")

          if not asserted.equals(actual)
            limit = (((errors[account_alias] ?= {})['limit'] ?= {}))
            limit[key] =
              expected: asserted.to_text()
              actual: actual.to_text()

  verify_infos: (errors, account_infos) ->
    for account in account_infos
      root = account.account_data
      account_alias = @am.lookup_alias root.Account
      asserted = @xrp_balances[account_alias]
      if asserted?
        actual = Amount.from_json root.Balance

        if not asserted.equals(actual)
          balance = (((errors[account_alias] ?= {})['balance'] ?= {}))
          balance['XRP'] =
            expected: asserted.to_human()
            actual:   actual.to_human()

  verify_offers: (errors, account_offers) ->
    for account in account_offers
      account_alias = @am.lookup_alias account.account
      get_errors = -> (((errors[account_alias] ?= {})['offers'] ?= []))

      assertions = @offers[account_alias]
      continue if not assertions?

      amount_text = (amt) => @am.realias_issuer amt.to_text_full()

      for asserted, ix in assertions
        offer = account.offers[ix]

        if not offer?
          get_errors().push {expected: asserted, actual: 'missing'}
          continue
        else
          # expected_*
          [epays, egets] = (parse_amount a for a in asserted)

          # actual_*
          apays = Amount.from_json offer.taker_pays
          agets = Amount.from_json offer.taker_gets

          err = {}

          if not epays.equals apays
            pay_err = (err['taker_pays'] = {})
            pay_err['expected'] = amount_text epays
            pay_err['actual']   = amount_text apays

          if not egets.equals agets
            get_err = (err['taker_gets'] = {})
            get_err['expected'] = amount_text egets
            get_err['actual']   = amount_text agets

          if Object.keys(err).length > 0
            offer_errors = get_errors()
            offer_errors.push err

  verify:  (account_infos, account_lines, account_offers) ->
    errors = {}

    # console.log @am.pretty_json account_infos
    # console.log @am.pretty_json account_lines
    # console.log @am.pretty_json account_offers

    @verify_infos errors, account_infos
    @verify_lines errors, account_lines
    @verify_offers errors, account_offers

    errors

  do_verify: (done) ->
    args_from_keys = (obj) -> ([a] for a in Object.keys obj)

    reqs = @requester

    lines_args = args_from_keys @iou_balances
    info_args = args_from_keys @xrp_balances
    offers_args = args_from_keys @offers

    async.series [
      (cb) ->
        reqs.requester(Remote::request_account_info,  info_args, cb)
      (cb) ->
        reqs.requester(Remote::request_account_lines, lines_args, cb)
      (cb) ->
        reqs.requester(Remote::request_account_offers, offers_args, cb)
    ], (error, results) =>
      assert !error,
             "There was an error @ #{error}"

      done(@verify(results...))

  compile_declaration: ->
    @offers = {}
    @xrp_balances = {}
    @iou_balances = {}
    @trusts = {}
    @realias_issuer = @am.realias_issuer

    record_amount = (account_id, to, amt) =>
      key = @am.amount_key amt
      ensure(account_id, to)[key] = amt

    for account_id, account of @declaration.accounts
      if account.offers?
        @offers[account_id] = account.offers
      if Array.isArray(account.balance)
        for value in account.balance
          balance = new Balance(value)
          if balance.is_native
            @xrp_balances[account_id] = balance.amount
          else
            if balance.limit?
              record_amount account_id, @trusts, balance.limit
            record_amount account_id, @iou_balances, balance.amount
