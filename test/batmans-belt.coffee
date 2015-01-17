################################### REQUIRES ###################################

fs        = require 'fs'
testutils = require './testutils'

################################### REQUIRES ###################################

exports.pretty_json = (v) -> JSON.stringify(v, undefined, 2)

exports.beast_configured = beast_configured = (k, v) ->
  '''

  A very naive regex search in $repo_root/src/BeastConfig.h

  @k the name of the macro
  @v the value as a string expected

  @return {Boolean} k's configured value, trimmed, is v

  '''
  beast_configured.buf ?= fs.readFileSync("#{__dirname}/../src/BeastConfig.h")
  pattern = "^#define\\s+#{k}\\s+(.*?)$"
  res = (new RegExp(pattern, 'm').exec(beast_configured.buf))
  return false if res == null
  actual = res[1].trim()
  return v == actual

exports.server_setup_teardown =  (options) ->
  {setup_func, teardown_func, post_setup, server_opts} = options ? {}

  context = null
  setup_func ?= setup
  teardown_func ?= teardown

  setup_func (done) ->
    context = @
    testutils.build_setup(server_opts).call @, ->
      if post_setup?
        post_setup(context, done)
      else
        done()

  teardown_func (done) ->
    testutils.build_teardown().call context, done

  # We turn a function to access the `context`, if we returned it now, it
  # would be null (DUH ;)
  -> context

exports.str_ends_with = ends_with = (s, suffix) ->
  ~s.indexOf(suffix, s.length - suffix.length)

exports.skip_or_only = (title, test_or_suite) ->
  if ends_with title, '_only'
    test_or_suite.only
  else if ends_with title, '_skip'
    test_or_suite.skip
  else
    test_or_suite

exports.is_focused_test =  (test_name) -> ends_with test_name, '_only'

class BailError extends Error
  constructor: (@message) ->
    @message ?= "Failed test due to relying on prior failed tests"

exports.suite_test_bailer = () ->
  bailed = false
  bail = (e) -> bailed = true

  suiteSetup ->
    process.on 'uncaughtException', bail

  suiteTeardown ->
    process.removeListener 'uncaughtException', bail

  wrapper = (test_func) ->
    test = (title, fn) ->
      wrapped = (done) ->
        if not bailed
          fn(done)
        else
          # We could do this, but it's just noisy
          if process.env.BAIL_PASSES
            done()
          else
            done(new BailError)
      test_func title, wrapped

    test.only = test_func.only
    test.skip = test_func.skip

    return test

  wrapper(global.test)

exports.submit_for_final = (tx, done) ->
  '''

  This helper submits a transaction, and once it's proposed sends a ledger
  accept so the transaction will finalize.

  '''
  tx.on 'submitted', (m) ->
    ter = (m.engine_result ? '').slice(0, 3)
    if ter in ['tes', 'tec']
      tx.remote.ledger_accept()
  tx.on 'final', (m) -> done(m)
  tx.submit()
