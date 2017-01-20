################################### REQUIRES ###################################

extend                     = require 'extend'
fs                         = require 'fs'
assert                     = require 'assert'
{
  Amount
  UInt160
  Transaction
}                          = require 'ripple-lib'


testutils                  = require './testutils'
{
  LedgerState
  LedgerVerifier
  TestAccount
}                          = require './ledger-state'
{
  beast_configured
  is_focused_test
  pretty_json
  server_setup_teardown
  skip_or_only
  str_ends_with
  submit_for_final
}                          = require './batmans-belt'

#################################### CONFIG ####################################

config = testutils.init_config()

#################################### HELPERS ###################################

make_offer = (remote, account, pays, gets, flag_or_flags) ->
  tx = remote.transaction()
  tx.offer_create(account, pays, gets)
  tx.set_flags(flag_or_flags) if flag_or_flags?
  tx

dump_rpc_script = (ledger_state, test_decl) ->
  lines = ledger_state.compile_to_rpc_commands()

  # Realias etc ;)
  # TODO
  account = test_decl.offer[0]
  [pays, gets, flags] = test_decl.offer[1..]
  tx = new Transaction({secrets: {}})
  tx.offer_create(account, pays, gets)
  tx.set_flags(flags)

  tx_json = tx.tx_json
    # Account: account
    # TransactionType: "OfferCreate"
    # TakerPays: pays
    # TakerGets: gets

  lines += "\nbuild/rippled submit #{account} '#{JSON.stringify tx_json}'"
  lines += "\nbuild/rippled ledger_accept\n"
  fs.writeFileSync(__dirname + '/../manual-offer-test.sh', lines)

dump_aliased_ledger = (pre_or_post, ledger_state, done) ->
  # TODO: generify to post/pre
  ledger_state.remote.request_ledger 'validated', {full: true}, (e, m) ->
    ledger_dump = ledger_state.pretty_json m.ledger.accountState
    fn = __dirname + "/../manual-offer-test-#{pre_or_post}-ledger.json"
    fs.writeFileSync(fn, ledger_dump)
    done()

################################# TEST FACTORY #################################

make_offer_create_test = (get_context, test_name, test_decl) ->
  '''
  
  @get_context      {Function}

    a getter function, which gets the current context with the ripple-lib remote
    etc attached

  @test_name        {String}
    
    This function will create a `test` using @test_name based on @test_decl

  @test_decl        {Object}

    @pre_ledger
    @post_ledger
    @offer

  '''
  test_func    = skip_or_only test_name, test
  focused_test = is_focused_test test_name

  test_func test_name, (done) ->
    context = get_context()

    remote       = context.remote
    ledger_state = context.ledger
    tx           = make_offer(remote, test_decl.offer...)

    submit_for_final tx, (m) ->
      'assert transaction was successful'
      assert.equal m.metadata.TransactionResult, 'tesSUCCESS'

      context.ledger.verifier(test_decl.post_ledger).do_verify (errors) ->
        this_done = ->
          assert Object.keys(errors).length == 0,
                 "post_ledger errors:\n"+ pretty_json errors
          done()

        if focused_test
          dump_aliased_ledger('post', ledger_state, this_done)
        else
          this_done()
  test_func

ledger_state_setup = (get_context, decls) ->
  setup (done) ->
    [test_name, test_decl] = decls.shift()

    context = get_context()
    focused_test = is_focused_test test_name

    context.ledger =
      new LedgerState(test_decl.pre_ledger, assert, context.remote, config)

    if focused_test
      dump_rpc_script(context.ledger, test_decl)

    context.ledger.setup(
      # console.log
      ->, # noop logging function
      ->
        context.ledger.verifier().do_verify (errors) ->
          assert Object.keys(errors).length == 0,
                 "pre_ledger errors:\n"+ pretty_json errors

          if focused_test
            dump_aliased_ledger('pre', context.ledger, done)
          else
            done()
    )

############################### TEST DECLARATIONS ##############################

try
  offer_create_tests = require("./offer-tests-json")
  # console.log offer_create_tests
  # offer_create_tests = JSON.parse offer_tests_string
  extend offer_create_tests, {}
catch e
  console.log e

if beast_configured('RIPPLE_ENABLE_AUTOBRIDGING', '1')
  suite_func = suite
else
  suite_func = suite.skip

suite_func 'Offer Create Tests', ->
  try
    get_context = server_setup_teardown()
    # tests = ([k,v] for k,v of offer_create_tests)

    tests = []
    only = false
    for k,v of offer_create_tests
      f = make_offer_create_test(get_context, k, v)
      if not only and f == test.only
        only = [[k, v]]
      if not str_ends_with k, '_skip'
        tests.push [k,v]

    # f = make_offer_create_test(get_context, k, v) for [k,v] in tests
    ledger_state_setup(get_context, if only then only else tests)
  catch e
    console.log e

