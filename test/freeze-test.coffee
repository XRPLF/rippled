################################### REQUIRES ###################################

extend                     = require 'extend'
fs                         = require 'fs'
assert                     = require 'assert'
async                      = require 'async'

{
  Remote
  UInt160
  Transaction
  Amount
}                          = require 'ripple-lib'

testutils                  = require './testutils'

{
  LedgerState
  LedgerVerifier
  TestAccount
}                          = require './ledger-state'

{
  pretty_json
  server_setup_teardown
  skip_or_only
  submit_for_final
  suite_test_bailer
}                          = require './batmans-belt'


################################ FREEZE OVERVIEW ###############################
'''

Freeze Feature Overview
=======================

A frozen line prevents funds from being transferred to anyone except back to the
issuer, yet does not prohibit acquisition of more of the issuer's assets, via
receipt of a Payment or placing offers to buy them.

A trust line's Flags now assigns two bits, for toggling the freeze status of
each side of a trustline.

GlobalFreeze
------------

There is also, a global freeze, toggled by a bit in the AccountRoot Flags, which
freezes ALL trust lines for an account.

Offers can not be created to buy or sell assets issued by an account with
GlobalFreeze set.

Use cases (via (David (JoelKatz) Schwartz)):

  There are two basic cases. One is a case where some kind of bug or flaw causes
  a large amount of an asset to somehow be created and the gateway hasn't yet
  decided how it's going to handle it.

  The other is a reissue where one asset is replaced by another. In a community
  credit case, say someone tricks you into issuing a huge amount of an asset,
  but you set the no freeze flag. You can still set global freeze to protect
  others from trading valuable assets for assets you issued that are now,
  unfortunately, worthless. And if you're an honest guy, you can set up a new
  account and re-issue to people who are legitimate holders

NoFreeze
--------

NoFreeze, is a set only flag bit in the account root.

When this bit is set:
  An account may not freeze its side of a trustline

  The NoFreeze bit can not be cleared

  The GlobalFreeze flag bit can not cleared
    GlobalFreeze can be used as a matter of last resort

Flag Definitions
================

LedgerEntry flags
-----------------

RippleState

  LowFreeze     0x00400000
  HighFreeze    0x00800000

AccountRoot

  NoFreeze      0x00200000
  GlobalFeeze   0x00400000

Transaction flags
-----------------

TrustSet (used with Flags)

  SetFreeze     0x00100000
  ClearFreeze   0x00200000

AccountSet (used with SetFlag/ClearFlag)

    NoFreeze              6
    GlobalFreeze          7

API Implications
================

transaction.Payment
-------------------

Any offers containing frozen funds found in the process of a tesSUCCESS will be
removed from the books.

transaction.OfferCreate
-----------------------

Selling an asset from a globally frozen issuer fails with tecFROZEN
Selling an asset from a frozen line fails with tecUNFUNDED_OFFER

Any offers containing frozen funds found in the process of a tesSUCCESS will be
removed from the books.

request.book_offers
-------------------

All offers selling assets from a frozen line/acount (offers created before a
freeze) will be filtered, except where in a global freeze situation where:

  TakerGets.issuer == Account ($frozen_account)

request.path_find & transaction.Payment
---------------------------------------

No Path may contain frozen trustlines, or offers (placed, prior to freezing) of
assets from frozen lines.

request.account_offers
-----------------------

These offers are unfiltered, merely walking the owner directory and reporting
all offers.

'''

################################################################################

Flags =
  sle:
    AccountRoot:
      PasswordSpent:   0x00010000
      RequireDestTag:  0x00020000
      RequireAuth:     0x00040000
      DisallowXRP:     0x00080000
      NoFreeze:        0x00200000
      GlobalFreeze:    0x00400000

    RippleState:
      LowFreeze:      0x00400000
      HighFreeze:     0x00800000
  tx:
    SetFlag:
      AccountRoot:
        NoFreeze:     6
        GlobalFreeze: 7

    TrustSet:
      # New Flags
      SetFreeze:      0x00100000
      ClearFreeze:    0x00200000


Transaction.flags.TrustSet ||= {};
# Monkey Patch SetFreeze and ClearFreeze into old version of ripple-lib
Transaction.flags.TrustSet.SetFreeze = Flags.tx.TrustSet.SetFreeze
Transaction.flags.TrustSet.ClearFreeze = Flags.tx.TrustSet.ClearFreeze

GlobalFreeze = Flags.tx.SetFlag.AccountRoot.GlobalFreeze
NoFreeze     = Flags.tx.SetFlag.AccountRoot.NoFreeze

#################################### CONFIG ####################################

config = testutils.init_config()

#################################### HELPERS ###################################

get_lines = (remote, acc, done) ->
  args = {account: acc, ledger: 'validated'}
  remote.request_account_lines args, (err, lines) ->
    done(lines)

account_set_factory = (remote, ledger, alias_for) ->
  (acc, fields, done) ->
    tx = remote.transaction()
    tx.account_set(acc)
    extend tx.tx_json, fields

    tx.on 'error', (err) ->
      assert !err, ("Unexpected error #{ledger.pretty_json err}\n" +
                    "don't use this helper if expecting an error")

    submit_for_final tx, (m) ->
      assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
      affected_root  = m.metadata.AffectedNodes[0].ModifiedNode
      assert.equal alias_for(affected_root.FinalFields.Account), acc
      done(affected_root)

make_payment_factory = (remote, ledger) ->
  (src, dst, amount, path, on_final) ->

    if typeof path == 'function'
      on_final = path
      path = undefined

    src_account = UInt160.json_rewrite src
    dst_account = UInt160.json_rewrite dst
    dst_amount  = Amount.from_json amount

    tx = remote.transaction().payment(src_account, dst_account, dst_amount)
    if not path?
      tx.build_path(true)
    else
      tx.path_add path.path
      tx.send_max path.send_max

    tx.on 'error', (err) ->
      if err.engine_result?.slice(0,3) == 'tec'
        # We can handle this in the `final`
        return
      assert !err, ("Unexpected error #{ledger.pretty_json err}\n" +
                    "don't use this helper if expecting an error")

    submit_for_final tx, (m) ->
      on_final m

create_offer_factory = (remote, ledger) ->
  (acc, pays, gets, on_final) ->
    tx = remote.transaction().offer_create(acc, pays, gets)

    tx.on 'error', (err) ->
      if err.engine_result?.slice(0,3) == 'tec'
        # We can handle this in the `final`
        return
      assert !err, ("Unexpected error #{ledger.pretty_json err}\n" +
                    "don't use this helper if expecting an error")
    submit_for_final tx, (m) ->
      on_final m

ledger_state_setup = (pre_ledger) ->
  post_setup = (context, done) ->
    context.ledger = new LedgerState(pre_ledger,
                                     assert,
                                     context.remote,
                                     config)

    context.ledger.setup(
      #-> # noop logging function
      ->
      ->
        context.ledger.verifier().do_verify (errors) ->
          assert Object.keys(errors).length == 0,
                 "pre_ledger errors:\n"+ pretty_json errors
          done()
    )

verify_ledger_state = (ledger, remote, pre_state, done) ->
  {config, assert, am} = ledger
  verifier = new LedgerVerifier(pre_state, remote, config, assert, am)

  verifier.do_verify (errors) ->
    assert Object.keys(errors).length == 0,
           "ledger_state errors:\n"+ pretty_json errors
    done()


book_offers_factory = (remote) ->
  (pays, gets, on_book) ->
    asset = (a) ->
      if typeof a == 'string'
        ret = {}
        [ret['currency'], ret['issuer']] = a.split('/')
        ret
      else
        a

    book=
      pays: asset(pays)
      gets: asset(gets)

    remote.request_book_offers book, (err, book) ->
      if err
        assert !err, "error with request_book_offers #{err}"

      on_book(book)

suite_setup = (state) ->
  '''

  @state

    The ledger state to setup, after starting the server

  '''
  opts =
    setup_func: suiteSetup
    teardown_func: suiteTeardown
    post_setup: ledger_state_setup(state)

  get_context = server_setup_teardown(opts)

  helpers      = null
  helpers_factory = ->
    context = {ledger, remote} = get_context()

    alog      = (obj) -> console.log ledger.pretty_json obj
    lines_for = (acc) -> get_lines(remote, arguments...)
    alias_for = (acc) -> ledger.am.lookup_alias(acc)

    verify_ledger_state_before_suite = (pre) ->
      suiteSetup (done) -> verify_ledger_state(ledger, remote, pre, done)

    {
      context:      context
      remote:       remote
      ledger:       ledger
      lines_for:    lines_for
      alog:         alog
      alias_for:    alias_for
      book_offers:  book_offers_factory(remote)
      create_offer: create_offer_factory(remote, ledger, alias_for)
      account_set:  account_set_factory(remote, ledger, alias_for)
      make_payment: make_payment_factory(remote, ledger, alias_for)
      verify_ledger_state_before_suite: verify_ledger_state_before_suite
    }

  get_helpers = -> (helpers = helpers ? helpers_factory())

  {
    get_context: get_context
    get_helpers: get_helpers
  }

##################################### TESTS ####################################

execute_if_enabled = (fn) ->
  enforced = true # freeze tests enforced
  fn(global.suite, enforced)

conditional_test_factory = ->
  test = suite_test_bailer()
  test_if = (b, args...) ->
    if b
      test(args...)
    else
      test.skip(args...)
  [test, test_if]

execute_if_enabled (suite, enforced) ->
  suite 'Freeze Feature', ->
    suite 'RippleState Freeze', ->
      # test = suite_test_bailer()
      [test, test_if] = conditional_test_factory()
      h = null

      {get_helpers} = suite_setup
        accounts:
          G1: balance: ['1000.0']

          bob:
            balance: ['1000.0', '10-100/USD/G1']

          alice:
            balance: ['1000.0', '100/USD/G1']
            offers: [['500.0', '100/USD/G1']]

      suite 'Account with line unfrozen (proving operations normally work)', ->
        test 'can make Payment on that line',  (done) ->
          {remote} = h = get_helpers()

          h.make_payment 'alice', 'bob', '1/USD/G1', (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            done()

        test 'can receive Payment on that line',  (done) ->
          h.make_payment 'bob', 'alice', '1/USD/G1', (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            done()

      suite 'Is created via a TrustSet with SetFreeze flag', ->
        test 'sets LowFreeze | HighFreeze flags', (done) ->
          {remote} = h

          tx = remote.transaction()
          tx.ripple_line_set('G1', '0/USD/bob')
          tx.set_flags('SetFreeze')

          submit_for_final tx, (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            affected = m.metadata.AffectedNodes
            assert affected.length > 1, "only one affected node implies a noop"
            ripple_state = affected[1].ModifiedNode
            final = ripple_state.FinalFields
            assert.equal h.alias_for(final.LowLimit.issuer), 'G1'
            assert final.Flags & Flags.sle.RippleState.LowFreeze
            assert !(final.Flags & Flags.sle.RippleState.HighFreeze)

            done()

      suite 'Account with line frozen by issuer', ->
        test 'can buy more assets on that line', (done) ->
          h.create_offer 'bob', '5/USD/G1', '25.0', (m) ->
            meta = m.metadata
            assert.equal meta.TransactionResult, 'tesSUCCESS'
            line = meta.AffectedNodes[3]['ModifiedNode'].FinalFields
            assert.equal h.alias_for(line.HighLimit.issuer), 'bob'
            assert.equal line.Balance.value, '-15' # HighLimit means balance inverted
            done()

        test_if enforced, 'can not sell assets from that line', (done) ->
          h.create_offer 'bob', '1.0', '5/USD/G1', (m) ->
            assert.equal m.engine_result, 'tecUNFUNDED_OFFER'
            done()

        test 'can receive Payment on that line',  (done) ->
          h.make_payment 'alice', 'bob', '1/USD/G1', (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            done()

        test_if enforced, 'can not make Payment from that line', (done) ->
          h.make_payment 'bob', 'alice', '1/USD/G1', (m) ->
            assert.equal m.engine_result, 'tecPATH_DRY'
            done()

      suite 'request_account_lines', ->
        test 'shows `freeze_peer` and `freeze` respectively', (done) ->
          async.parallel [
            (next) ->
                h.lines_for 'G1', (lines) ->
                  for line in lines.lines
                    if h.alias_for(line.account) == 'bob'
                      assert.equal line.freeze, true
                      assert.equal line.balance, '-16'
                      # unless we get here, the test will hang alerting us to
                      # something amiss
                      next() # setImmediate ;)
                      break

            (next) ->
              h.lines_for 'bob', (lines) ->
                for line in lines.lines
                  if h.alias_for(line.account) == 'G1'
                    assert.equal line.freeze_peer, true
                    assert.equal line.balance, '16'
                    next()
                    break
          ], ->
            done()

      suite 'Is cleared via a TrustSet with ClearFreeze flag', ->
        test 'sets LowFreeze | HighFreeze flags', (done) ->
          {remote} = h

          tx = remote.transaction()
          tx.ripple_line_set('G1', '0/USD/bob')
          tx.set_flags('ClearFreeze')

          submit_for_final tx, (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            affected = m.metadata.AffectedNodes
            ripple_state = affected[1].ModifiedNode
            final = ripple_state.FinalFields
            assert.equal h.alias_for(final.LowLimit.issuer), 'G1'
            assert !(final.Flags & Flags.sle.RippleState.LowFreeze)
            assert !(final.Flags & Flags.sle.RippleState.HighFreeze)

            done()

    suite 'Global (AccountRoot) Freeze', ->
      # NoFreeze:        0x00200000
      # GlobalFreeze:    0x00400000

      # test = suite_test_bailer()
      [test, test_if] = conditional_test_factory()
      h = null

      {get_helpers} = suite_setup
        accounts:
          G1:
            balance: ['12000.0']
            offers: [['10000.0', '100/USD/G1'], ['100/USD/G1', '10000.0']]

          A1:
            balance: ['1000.0', '1000/USD/G1']
            offers: [['10000.0', '100/USD/G1']]
            trusts: ['1200/USD/G1']

          A2:
            balance: ['20000.0', '100/USD/G1']
            trusts: ['200/USD/G1']
            offers: [['100/USD/G1', '10000.0']]

          A3:
            balance: ['20000.0', '100/BTC/G1']

          A4:
            balance: ['20000.0', '100/BTC/G1']

      suite 'Is toggled via AccountSet using SetFlag and ClearFlag', ->
        test 'SetFlag GlobalFreeze should set 0x00400000 in Flags', (done) ->
          {remote} = h = get_helpers()

          h.account_set 'G1', SetFlag: GlobalFreeze, (root) ->
            new_flags  = root.FinalFields.Flags

            assert !(new_flags & Flags.sle.AccountRoot.NoFreeze)
            assert (new_flags & Flags.sle.AccountRoot.GlobalFreeze)

            done()

        test 'ClearFlag GlobalFreeze should clear 0x00400000 in Flags', (done) ->
          {remote} = h = get_helpers()

          h.account_set 'G1', ClearFlag: GlobalFreeze, (root) ->
            new_flags  = root.FinalFields.Flags

            assert !(new_flags & Flags.sle.AccountRoot.NoFreeze)
            assert !(new_flags & Flags.sle.AccountRoot.GlobalFreeze)

            done()

      suite 'Account without GlobalFreeze (proving operations normally work)', ->
        suite 'have visible offers', ->
          test 'where taker_gets is $unfrozen_issuer', (done) ->
            {remote} = h = get_helpers()

            h.book_offers 'XRP', 'USD/G1', (book) ->
              assert.equal book.offers.length, 2

              aliases = (h.alias_for(o.Account) for o in book.offers).sort()

              assert.equal aliases[0], 'A1'
              assert.equal aliases[1], 'G1'

              done()

          test 'where taker_pays is $unfrozen_issuer', (done) ->
            h.book_offers 'USD/G1', 'XRP', (book) ->

              assert.equal book.offers.length, 2
              aliases = (h.alias_for(o.Account) for o in book.offers).sort()

              assert.equal aliases[0], 'A2'
              assert.equal aliases[1], 'G1'

              done()

        suite 'its assets can be', ->

          test 'bought on the market', (next) ->
            h.create_offer 'A3', '1/BTC/G1', '1.0', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              next()

          test 'sold on the market', (next) ->
            h.create_offer 'A4', '1.0', '1/BTC/G1', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              next()

        suite 'Payments', ->
          test 'direct issues can be sent',  (done) ->
            {remote} = h = get_helpers()

            h.make_payment 'G1', 'A2', '1/USD/G1', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              done()

          test 'direct redemptions can be sent',  (done) ->
            h.make_payment 'A2', 'G1', '1/USD/G1', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              done()

          test 'via rippling can be sent',  (done) ->
            h.make_payment 'A2', 'A1', '1/USD/G1', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              done()

          test 'via rippling can be sent back', (done) ->
            h.make_payment 'A2', 'A1', '1/USD/G1', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              done()

      suite 'Account with GlobalFreeze', ->
        suite 'Needs to set GlobalFreeze first', ->
          test 'SetFlag GlobalFreeze will toggle back to freeze', (done) ->
            h.account_set 'G1', SetFlag: GlobalFreeze, (root) ->
              new_flags  = root.FinalFields.Flags

              assert !(new_flags & Flags.sle.AccountRoot.NoFreeze)
              assert (new_flags  & Flags.sle.AccountRoot.GlobalFreeze)

              done()

        suite 'its assets can\'t be', ->
          test_if enforced, 'bought on the market', (next) ->
            h.create_offer 'A3', '1/BTC/G1', '1.0', (m) ->
              assert.equal m.engine_result, 'tecFROZEN'
              next()

          test_if enforced, 'sold on the market', (next) ->
            h.create_offer 'A4', '1.0', '1/BTC/G1', (m) ->
              assert.equal m.engine_result, 'tecFROZEN'
              next()

        suite 'its offers are filtered', ->
          test_if enforced, 'account_offers always '+
                            'shows their own offers', (done) ->
            {remote} = h = get_helpers()

            args = { account: 'G1', ledger: 'validated' }

            remote.request_account_offers args, (err, res) ->
              assert.equal res.offers.length, 2
              done()

          test.skip 'books_offers(*, $frozen_account/*) shows offers '+
               'owned by $frozen_account only', (done) ->

            h.book_offers 'XRP', 'USD/G1', (book) ->
              # h.alog book.offers
              assert.equal book.offers.length, 1
              done()

          test.skip 'books_offers($frozen_account/*, *) shows '+
                    'no offers', (done) ->

            h.book_offers 'USD/G1', 'XRP', (book) ->
              assert.equal book.offers.length, 0
              done()

          test_if enforced, 'books_offers(*, $frozen_account/*) shows offers '+
               'owned by $frozen_account only (broken) ', (done) ->

            h.book_offers 'XRP', 'USD/G1', (book) ->
              # h.alog book.offers
              assert.equal book.offers.length, 2
              done()

          test_if enforced, 'books_offers($frozen_account/*, *) '+
                            'shows no offers (broken)', (done) ->

            h.book_offers 'USD/G1', 'XRP', (book) ->
              assert.equal book.offers.length, 2
              done()

        suite 'Payments', ->
          test 'direct issues can be sent',  (done) ->
            {remote} = h = get_helpers()

            h.make_payment 'G1', 'A2', '1/USD/G1', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              done()

          test 'direct redemptions can be sent',  (done) ->
            h.make_payment 'A2', 'G1', '1/USD/G1', (m) ->
              assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
              done()

          test_if enforced, 'via rippling cant be sent',  (done) ->
            h.make_payment 'A2', 'A1', '1/USD/G1', (m) ->
              assert.equal m.engine_result, 'tecPATH_DRY'
              done()

    suite  'Accounts with NoFreeze', ->
      test = suite_test_bailer()
      h = null

      {get_helpers} = suite_setup
        accounts:
          G1:  balance: ['12000.0']
          A1:  balance: ['1000.0', '1000/USD/G1']

      suite 'TrustSet NoFreeze', ->
        test 'should set 0x00200000 in Flags', (done) ->
          h = get_helpers()

          h.account_set 'G1', SetFlag: NoFreeze, (root) ->
            new_flags  = root.FinalFields.Flags

            assert (new_flags & Flags.sle.AccountRoot.NoFreeze)
            assert !(new_flags & Flags.sle.AccountRoot.GlobalFreeze)

            done()

        test 'can not be cleared', (done) ->
          h.account_set 'G1', ClearFlag: NoFreeze, (root) ->
            new_flags  = root.FinalFields.Flags

            assert (new_flags & Flags.sle.AccountRoot.NoFreeze)
            assert !(new_flags & Flags.sle.AccountRoot.GlobalFreeze)

            done()

      suite 'GlobalFreeze', ->
        test 'can set GlobalFreeze', (done) ->
          h.account_set 'G1', SetFlag: GlobalFreeze, (root) ->
            new_flags  = root.FinalFields.Flags

            assert (new_flags & Flags.sle.AccountRoot.NoFreeze)
            assert (new_flags & Flags.sle.AccountRoot.GlobalFreeze)

            done()

        test 'can not unset GlobalFreeze', (done) ->
          h.account_set 'G1', ClearFlag: GlobalFreeze, (root) ->
            new_flags  = root.FinalFields.Flags

            assert (new_flags & Flags.sle.AccountRoot.NoFreeze)
            assert (new_flags & Flags.sle.AccountRoot.GlobalFreeze)

            done()

      suite 'their trustlines', ->
        test 'can\'t be frozen', (done) ->
          {remote} = h = get_helpers()

          tx = remote.transaction()
          tx.ripple_line_set('G1', '0/USD/A1')
          tx.set_flags('SetFreeze')

          submit_for_final tx, (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            affected = m.metadata.AffectedNodes
            assert.equal affected.length, 1
            affected_type = affected[0]['ModifiedNode'].LedgerEntryType
            assert.equal affected_type, 'AccountRoot'

            done()

    suite 'Offers for frozen trustlines (not GlobalFreeze)', ->
      # test = suite_test_bailer()
      [test, test_if] = conditional_test_factory()
      remote = h = null

      {get_helpers} = suite_setup
        accounts:
          G1:
            balance: ['1000.0']
          A2:
            balance: ['2000.0']
            trusts: ['1000/USD/G1']
          A3:
            balance: ['1000.0', '2000/USD/G1']
            offers: [['1000.0', '1000/USD/G1']]

          A4:
            balance: ['1000.0', '2000/USD/G1']

      suite 'will be removed by Payment with tesSUCCESS', ->
        test 'can normally make a payment partially consuming offer', (done) ->
          {remote} = h = get_helpers()

          path =
            path: [{"currency": "USD", "issuer": "G1"}]
            send_max: '1.0'

          h.make_payment 'A2', 'G1', '1/USD/G1', path, (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            done()

        test 'offer was only partially consumed', (done) ->
          args = { account: 'A3', ledger: 'validated' }

          remote.request_account_offers args, (err, res) ->
            assert res.offers.length == 1
            assert res.offers[0].taker_gets.value, '999'
            done()

        test 'someone else creates an offer providing liquidity', (done) ->
          h.create_offer 'A4', '999.0', '999/USD/G1', (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            affected = m.metadata.AffectedNodes
            done()

        test 'owner of partially consumed offer\'s line is frozen', (done) ->
          tx = remote.transaction()
          tx.ripple_line_set('G1', '0/USD/A3')
          tx.set_flags('SetFreeze')

          submit_for_final tx, (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            affected = m.metadata.AffectedNodes
            ripple_state = affected[1].ModifiedNode
            final = ripple_state.FinalFields
            assert.equal h.alias_for(final.HighLimit.issuer), 'G1'
            assert !(final.Flags & Flags.sle.RippleState.LowFreeze)
            assert (final.Flags & Flags.sle.RippleState.HighFreeze)

            done()

        test 'Can make a payment via the new offer', (done) ->
          path =
            path: [{"currency": "USD", "issuer": "G1"}]
            send_max: '1.0'

          h.make_payment 'A2', 'G1', '1/USD/G1', path, (m) ->
            # assert.equal m.engine_result, 'tecPATH_PARTIAL' # tecPATH_DRY
            assert.equal m.metadata.TransactionResult, 'tesSUCCESS' # tecPATH_DRY
            done()

        test_if enforced, 'Partially consumed offer was removed by tes* payment', (done) ->
          args = { account: 'A3', ledger: 'validated' }

          remote.request_account_offers args, (err, res) ->
            assert res.offers.length == 0
            done()

      suite 'will be removed by OfferCreate with tesSUCCESS', ->
        test_if enforced, 'freeze the new offer', (done) ->
          tx = remote.transaction()
          tx.ripple_line_set('G1', '0/USD/A4')
          tx.set_flags('SetFreeze')

          submit_for_final tx, (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            affected = m.metadata.AffectedNodes
            ripple_state = affected[0].ModifiedNode
            final = ripple_state.FinalFields
            assert.equal h.alias_for(final.LowLimit.issuer), 'G1'
            assert (final.Flags & Flags.sle.RippleState.LowFreeze)
            assert !(final.Flags & Flags.sle.RippleState.HighFreeze)

            done()

        test_if enforced, 'can no longer create a crossing offer', (done) ->
          h.create_offer 'A2', '999/USD/G1', '999.0', (m) ->
            assert.equal m.metadata?.TransactionResult, 'tesSUCCESS'
            affected = m.metadata.AffectedNodes
            created = affected[5].CreatedNode
            new_fields = created.NewFields
            assert.equal h.alias_for(new_fields.Account), 'A2'
            done()

        test_if enforced, 'offer was removed by offer_create', (done) ->
          args = { account: 'A4', ledger: 'validated' }

          remote.request_account_offers args, (err, res) ->
            assert res.offers.length == 0
            done()
