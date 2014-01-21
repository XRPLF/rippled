################################### REQUIRES ###################################

testutils     = require './testutils'
child_process = require 'child_process'
assert        = require 'assert'

#################################### CONFIG ####################################

config       = testutils.init_config()
rippled_path = config.default_server_config.rippled_path

DEBUG        = 0

setupFunc    = suiteSetup    # setup
teardownFunc = suiteTeardown # teardown

#################################### HELPERS ###################################

pretty_json = (value) ->
  JSON.stringify value, undefined, 2

##################################### MAIN #####################################

define_cmd_line_test_factory = (server) ->
  # A helper for running rippled `from the command line`
  do_run_command = (args) =>
    base_options =
      env:   process.env
      # We just want the stdout which will have the rpc responses in json,
      # the stderr stream
      stdio: ['ignore', 'pipe', 'ignore']
      # Make sure we use the standalone server config...
      cwd:   server().serverPath()

    child = child_process.spawn(rippled_path, args, base_options)
    return child

  # A higher level helper that transforms any object args into json before
  # sending
  run_cmd = ->
    args = []
    for arg in arguments
      if typeof arg == 'object'
        arg = JSON.stringify arg
      args.push arg
    do_run_command args

  n = 0
  # Define a `test` that will run a rippled command line eg.
  # $ ./build/rippled sign passphrase '{"TransactionType" : "Payment"}'
  sign_test = (secret, tx_json, error, debug) ->
    [error_message, code] = error

    test "Signing test (#{++n}): #{error_message} ", (done) ->
      child = run_cmd 'sign', secret, tx_json

      child.once 'exit', ->
        result = JSON.parse(child.stdout.read())
        console.log pretty_json(result) if DEBUG or debug?
        result = result.result

        assert_equal = (actual, expected, result) ->
          msg = "Expected `#{expected}` in\n #{pretty_json result}"
          assert.equal actual, expected, msg

        assert_equal result.error_message, error_message, result
        assert_equal result.error, code, result
        done()

  return sign_test

suite 'errorReporting', ->
  # This gives `var server = null` semantics for coffee script, so can set it
  # later inside a nested scoped
  server = null

  # The FactoryFactoryFactorater ;)
  sign_test = define_cmd_line_test_factory -> server

  # We need to setup a server running in standalone mode, so we can subsequently
  # run `rippled $cmd $param_n $param_n+1 ...
  setupFunc (done) ->
    testutils.build_setup().call @, =>
      server = @.store.alpha.server
      done()

  # Tear it down once all the tests have run
  # suiteTeardown (done) ->
  teardownFunc (done) ->
    console.log "suiteTeardown"
    testutils.build_teardown().call @, ->
      done()

  sign_test 'passphrase',
            {},
            ['Missing field \'tx_json.TransactionType\'.', 'invalidParams']

  sign_test 'passphrase',
            {TransactionType: "Payment"},
            ['Missing field \'tx_json.Account\'.', 'srcActMissing']

  sign_test 'passphrase',
            {
              TransactionType: "Payment",
              # Using an account that doesn't exist in standalone server
              # ledger
              Account: "rMTzGg7nPPEMJthjgEBfiPZGoAM7MEVa1r"
            },
            ['Source account not found.', 'srcActNotFound']

  sign_test 'passphrase',
            {
              TransactionType: "Payment",
              # Use the root account which WOULD exist
              Account: "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
              Amount: "1"
            },
            ["Missing field 'tx_json.Destination'.", 'invalidParams']

  sign_test 'passphrase',
            {
              TransactionType: "Payment",
              Account: "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
              Destination: "rMTzGg7nPPEMJthjgEBfiPZGoAM7MEVa1r"
              Amount: "1"
            },
            ['Secret does not match account.', 'badSecret']

  sign_test 'masterpassphrase',
            {
              TransactionType: "Payment",
              Account: "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
              Destination: "rMTzGg7nPPEMJthjgEBfiPZGoAM7MEVa1r"
              # We don't have an Amount, so we'd expect some error handling
              # here (or earlier really ...)
            },
            ['Missing field \'tx_json.Amount\'.', 'invalidParams']