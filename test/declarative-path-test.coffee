################################### REQUIRES ###################################

extend                     = require 'extend'
fs                         = require 'fs'
async                      = require 'async'
deep_eq                    = require 'deep-equal'

{Amount
 Remote
 Seed
 Base
 Transaction
 PathFind
 sjcl
 UInt160}                  = require 'ripple-lib'

testutils                  = require './testutils'
{Server}                   = require './server'
{LedgerState, TestAccount} = require './ledger-state'
{test_accounts}            = require './random-test-addresses'

simple_assert              = require 'assert'

#################################### README ####################################
"""
The tests are written in a declarative style:
  
  Each case has an entry in the `path_finding_cases` object
  The key translates to a `suite(key, {...})`
  The `{...}` passed in is compiled into a setup/teardown for the `ledger` and
  into a bunch of `test` invocations for the `paths_expected`
    
    - aliases are used throughout for easier reading

      - test account addresses will be created `on the fly`
         
         no need to declare in testconfig.js
         debugged responses from the server substitute addresses for aliases

    - The fixtures are setup just once for each ledger, multiple path finding
      tests can be executed
     
    - `paths_expected` top level keys are group names
                       2nd level keys are path test declarations
                       
                       test declaration keys can be suffixed meaningfully with
                       
                          `_skip`
                          `_only`
                          
                        test declaration values can set
                        
                          debug: true
                          
                            Will dump the path declaration and 
                            translated request and subsequent response

    - hops in `alternatives[*][paths][*]` can be written in shorthand
        eg.
          ABC/G3|G3
            get `ABC/G3` through `G3`

          ABC/M1|M1
            get `ABC/M1` through `M1`

          XRP|$
            get `XRP` through `$` 
                              $ signifies an order book rather than account
  
  ------------------------------------------------------------------------------
  Tests can be written in the 'path-tests-json.js' file in same directory   # <--
  ------------------------------------------------------------------------------
"""
#################################### HELPERS ###################################

assert = simple_assert

refute = (cond, msg) -> assert(!cond, msg)
prettyj = pretty_json =  (v) -> JSON.stringify(v, undefined, 2)

propagater = (done) ->
  (f) ->
    ->
      return if done.aborted
      try
        f(arguments...)
      catch e
        done.aborted = true
        throw e

assert_match = (o, key_vals, message) ->
  """
  assert_match path[i], matcher,
               "alternative[#{ai}].paths[#{pi}]"
  """

  for k,v of key_vals
    assert.equal o[k], v, message

#################################### CONFIG ####################################

config = testutils.init_config()

############################### ALTERNATIVES TEST ##############################

expand_alternative = (alt) ->
  """
  
  Make explicit the currency and issuer in each hop in paths_computed
  
  """
  amt = Amount.from_json(alt.source_amount)

  for path in alt.paths_computed
    prev_issuer = amt.issuer().to_json()
    prev_currency = amt.currency().to_json()

    for hop, hop_i in path
      if not hop.currency?
        hop.currency = prev_currency

      if not hop.issuer? and hop.currency != 'XRP'
        if hop.account?
          hop.issuer = hop.account
        else
          hop.issuer = prev_issuer

      if hop.type & 0x10
        prev_currency = hop.currency

      if hop.type & 0x20
        prev_issuer = hop.issuer
      else if hop.account?
        prev_issuer = hop.account

  return alt

create_shorthand = (alternatives) ->
  """
  
  Convert explicit paths_computed into the format used by `paths_expected`
  These can be pasted in as the basis of tests.
  
  """
  shorthand = []

  for alt in alternatives
    short_alt = {}
    shorthand.push short_alt

    amt = Amount.from_json alt.source_amount
    if amt.is_native()
      short_alt.amount = amt.to_human()
      if not (~short_alt.amount.search('.'))
        short_alt.amount = short_alt.amount + '.0'
    else
      short_alt.amount = amt.to_text_full()

    short_alt.paths    = []

    for path in alt.paths_computed
      short_path = []
      short_alt.paths.push short_path

      for node in path
        hop = node.currency
        hop = "#{hop}/#{node.issuer}" if node.issuer?
        hop = "#{hop}|#{if node.account? then node.account else "$"}"
        short_path.push hop

  return shorthand

ensure_list = (v) ->
  if Array.isArray(v)
    v
  else
    [v]

test_alternatives_factory = (realias_pp, realias_text) ->
  """
  
  We are using a factory to create `test_alternatives` because it needs the 
  per ledger `realias_*` functions

  """
  hop_matcher = (decl_hop) ->
    [ci, f] = decl_hop.split('|')
    if not f?
        throw new Error("No `|` in #{decl_hop}")

    [c,  i] = ci.split('/')
    is_account = if f == '$' then false else true
    matcher = currency: c
    matcher.issuer = i if i?
    matcher.account = f if is_account
    matcher

  match_path = (test, path, ai, pi) ->
    test = (hop_matcher(hop) for hop in test)
    assert.equal path.length, test.length,
                  "alternative[#{ai}] path[#{pi}] expecting #{test.length} hops"

    for matcher, i in test
      assert_match path[i], matcher,
                   "alternative[#{ai}].paths[#{pi}]"
    return

  simple_match_path = (test, path, ai, pi) ->
    """
    
      @test

        A shorthand specified path

      @path
        
        A path as returned by the server with `expand_alternative` done
        so issuer and currency are always stated.
    
    """
    test = (hop_matcher(hop) for hop in test)
    return false if not test.length == path.length

    for matcher, i in test
      for k, v of matcher
        return false if not path[i]?
        if path[i][k] != v
          return false
    true

  amounts = ->
    (Amount.from_json a for a in arguments)

  amounts_text = ->
    (realias_text a.to_text_full() for a in arguments)

  check_for_no_redundant_paths = (alternatives) ->
    for alt, i in alternatives
      existing_paths = []
      for path in alt.paths_computed
        for existing in existing_paths
          assert !(deep_eq path, existing),
                 "Duplicate path in alternatives[#{i}]\n"+
                 "#{realias_pp alternatives[0]}"

        existing_paths.push path
    return

  test_alternatives = (test, actual, error_context) ->
    """
    
      @test
        alternatives in shorthand format

      @actual
        alternatives as returned in a `path_find` response
        
      @error_context
      
        a function providing a string with extra context to provide to assertion
        messages

    """
    check_for_no_redundant_paths actual

    for t, ti in ensure_list(test)
      a = actual[ti]
      [t_amt, a_amt] = amounts(t.amount, a.source_amount)
      [t_amt_txt, a_amt_txt] = amounts_text(t_amt, a_amt)

      # console.log typeof t_amt

      assert t_amt.equals(a_amt),
             "Expecting alternative[#{ti}].amount: "+
             "#{t_amt_txt} == #{a_amt_txt}"

      t_paths = ensure_list(t.paths)

      tn = t_paths.length
      an = a.paths_computed.length
      assert.equal tn, an, "Different number of paths specified for alternative[#{ti}]"+
                            ", expected: #{prettyj t_paths}, "+
                            "actual(shorthand): #{prettyj create_shorthand actual}"+
                            "actual(verbose): #{prettyj a.paths_computed}"+
                            error_context()

      for p, i in t_paths
        matched = false

        for m in a.paths_computed
          if simple_match_path(p, m, ti, i)
            matched = true
            break

        assert matched, "Can't find a match for path[#{i}]: #{prettyj p} "+
                        "amongst #{prettyj create_shorthand [a]}"+
                        error_context()
    return

################################################################################

create_path_test = (pth) ->
  return (done) ->
    self    = this
    WHAT    = self.log_what
    ledger  = self.ledger
    test_alternatives = test_alternatives_factory ledger.pretty_json.bind(ledger),
                                                  ledger.realias_issuer


    WHAT "#{pth.title}: #{pth.src} sending #{pth.dst}, "+
         "#{pth.send}, via #{pth.via}"

    one_message = (f) ->
      self.remote._servers[0].once 'before_send_message_for_non_mutators', f

    sent = "TODO: need to patch ripple-lib"
    one_message (m) -> sent = m

    error_info = (m, more) ->
      info =
        path_expected:     pth,
        path_find_request: sent,
        path_find_updates: messages

      extend(info, more) if more?
      ledger.pretty_json(info)

    assert Amount.from_json(pth.send).is_valid(),
           "#{pth.send} is not valid Amount"

    _src = UInt160.json_rewrite(pth.src)
    _dst = UInt160.json_rewrite(pth.dst)
    _amt = Amount.from_json(pth.send)

    # self.server.clear_logs() "TODO: need to patch ripple-lib"
    options = {
      src_account: _src
      dst_account: _dst
      dst_amount: _amt,
      src_currencies: [{currency: pth.via}]
    }
    pf = self.remote.path_find(options)

    updates  = 0
    max_seen = 0
    messages = {}

    propagates = propagater done

    pf.on "error", propagates (m) ->                                       # <--
      assert false, "fail (error): #{error_info(m)}"
      done()

    pf.on "update", propagates (m) ->                                      # <--
      # TODO:hack:
      expand_alternative alt for alt in m.alternatives


      messages[if updates then "update-#{updates}" else 'initial-response'] = m
      updates++
      # console.log updates

      assert m.alternatives.length >= max_seen,
             "Subsequent path_find update' should never have less " +
             "alternatives:\n#{ledger.pretty_json messages}"

      max_seen = m.alternatives.length

      # if updates == 2
      #   testutils.ledger_close(self.remote, -> )

      if updates == 2
        # "TODO: need to patch ripple-lib"
        # self.log_pre(self.server.get_logs(), "Server Logs")

        if pth.do_send?
          do_send( (ledger.pretty_json.bind ledger), WHAT, self.remote, pth,
                   messages['update-2'], done )

        if pth.debug
          console.log ledger.pretty_json(messages)
          console.log error_info(m)
          console.log ledger.pretty_json create_shorthand(m.alternatives)

        if pth.alternatives?
          # We realias before doing any comparisons
          alts = ledger.realias_issuer(JSON.stringify(m.alternatives))
          alts = JSON.parse(alts)
          test = pth.alternatives

          assert test.length == alts.length,
                "Number of `alternatives` specified is different: "+
                "#{error_info(m)}"

          if test.length == alts.length
            test_alternatives(pth.alternatives, alts, -> error_info(m))

        if pth.n_alternatives?
          assert pth.n_alternatives ==  m.alternatives.length,
                 "fail (wrong n_alternatives): #{error_info(m)}"

        pf.close()
        done() if not pth.do_send?

################################ SUITE CREATION ################################

skip_or_only = (title, test_or_suite) ->
  endsWith = (s, suffix) ->
    ~s.indexOf(suffix, s.length - suffix.length)

  if endsWith title, '_only'
    test_or_suite.only
  else if endsWith title, '_skip'
    test_or_suite.skip
  else
    test_or_suite

definer_factory = (group, title, path) ->
  path.title = "#{[group, title].join('.')}"
  test_func = skip_or_only path.title, test
  ->
    test_func(path.title, create_path_test(path) )

gather_path_definers = (path_expected) ->
  tests = []
  for group, subgroup of path_expected
    for title, path of subgroup
      tests.push definer_factory(group, title, path)
  tests

suite_factory = (declaration) ->
  ->
    context = null

    suiteSetup (done) ->
      context = @
      @log_what = ->

      testutils.build_setup().call @, ->
        context.ledger = new LedgerState(declaration.ledger,
                                         assert,
                                         context.remote,
                                         config)

        context.ledger.setup(context.log_what, done)

    suiteTeardown (done) ->
      testutils.build_teardown().call context, done

    for definer in gather_path_definers(declaration.paths_expected)
      definer()

define_suites = (path_finding_cases) ->
  for case_name, declaration of path_finding_cases
    suite_func = skip_or_only case_name, suite
    suite_func case_name, suite_factory(declaration)

############################## PATH FINDING CASES ##############################
# Later we reference A0, the `unknown account`, directly embedding the full
# address.
A0 = (new TestAccount('A0')).address
assert A0 == 'rBmhuVAvi372AerwzwERGjhLjqkMmAwxX'

try
  path_finding_cases = require('./path-tests-json')
catch e
  console.log e

# You need two gateways, same currency. A market maker. A source that trusts one
# gateway and holds its currency, and a destination that trusts the other.

extend path_finding_cases,
  "CNY test":
    paths_expected:
      BS:
        P101: src: "SRC", dst: "GATEWAY_DST", send: "10.1/CNY/GATEWAY_DST", via: "XRP", n_alternatives: 1

    ledger:
      accounts:
        SRC:
          balance: ["4999.999898"]
          trusts: []
          offers: []

        GATEWAY_DST:
          balance: ["10846.168060"]
          trusts: []
          offers: []

        MONEY_MAKER_1:
          balance: ["4291.430036"]
          trusts: []
          offers: []

        MONEY_MAKER_2:
          balance: [
            "106839375770"
            "0.0000000003599/CNY/MONEY_MAKER_1"
            "137.6852546843001/CNY/GATEWAY_DST"
          ]
          trusts: [
            "1001/CNY/MONEY_MAKER_1"
            "1001/CNY/GATEWAY_DST"
          ]
          offers: [
            [
              "1000000"
              "1/CNY/GATEWAY_DST"
              # []
            ]
            [
              "1/CNY/GATEWAY_DST"
              "1000000"
              # []
            ]
            [
              "318000/CNY/GATEWAY_DST"
              "53000000000"
              # ["Sell"]
            ]
            [
              "209000000"
              "4.18/CNY/MONEY_MAKER_2"
              # []
            ]
            [
              "990000/CNY/MONEY_MAKER_1"
              "10000000000"
              # ["Sell"]
            ]
            [
              "9990000/CNY/MONEY_MAKER_1"
              "10000000000"
              # ["Sell"]
            ]
            [
              "8870000/CNY/GATEWAY_DST"
              "10000000000"
              # ["Sell"]
            ]
            [
              "232000000"
              "5.568/CNY/MONEY_MAKER_2"
              # []
            ]
          ]

        A1:
          balance: [
            # "240.997150"
            "1240.997150"
            "0.0000000119761/CNY/MONEY_MAKER_1"
            "33.047994/CNY/GATEWAY_DST"
          ]
          trusts: [
            "1000000/CNY/MONEY_MAKER_1"
            "100000/USD/MONEY_MAKER_1"
            "10000/BTC/MONEY_MAKER_1"
            "1000/USD/GATEWAY_DST"
            "1000/CNY/GATEWAY_DST"
          ]
          offers: []

        A2:
          balance: [
            "14115.046893"
            "209.3081873019994/CNY/MONEY_MAKER_1"
            "694.6251706504019/CNY/GATEWAY_DST"
          ]
          trusts: [
            "3000/CNY/MONEY_MAKER_1"
            "3000/CNY/GATEWAY_DST"
          ]
          offers: [
            [
              "2000000000"
              "66.8/CNY/MONEY_MAKER_1"
              # []
            ]
            [
              "1200000000"
              "42/CNY/GATEWAY_DST"
              # []
            ]
            [
              "43.2/CNY/MONEY_MAKER_1"
              "900000000"
              # ["Sell"]
            ]
          ]

        A3:
          balance: [
            "512087.883181"
            "23.617050013581/CNY/MONEY_MAKER_1"
            "70.999614649799/CNY/GATEWAY_DST"
          ]
          trusts: [
            "10000/CNY/MONEY_MAKER_1"
            "10000/CNY/GATEWAY_DST"
          ]
          offers: [[
            "2240/CNY/MONEY_MAKER_1"
            "50000000000"
            # ["Sell"]
          ]]


  "Path Tests (Bitstamp + SnapSwap account holders | liquidity provider with no offers)":
    ledger:
      accounts:
        G1BS:
          balance: ["1000.0"]
        G2SW:
          balance: ["1000.0"]
        A1:
          balance: ["1000.0", "1000/HKD/G1BS"]
          trusts: ["2000/HKD/G1BS"]
        A2:
          balance: ["1000.0", "1000/HKD/G2SW"]
          trusts: ["2000/HKD/G2SW"]
        M1:
          # SnapSwap wants to be able to set trust line quality settings so they
          # can charge a fee when transactions ripple across. Liquitidy
          # provider, via trusting/holding both accounts
          balance: ["11000.0",
                   "1200/HKD/G1BS",
                   "5000/HKD/G2SW"
          ]
          trusts: ["100000/HKD/G1BS", "100000/HKD/G2SW"]
          # We haven't got ANY offers

    paths_expected: {
      BS:
        P1:
          debug: false
          src: "A1", dst: "A2", send: "10/HKD/A2", via: "HKD"
          n_alternatives: 1
        P2:
          debug: false
          src: "A2", dst: "A1", send: "10/HKD/A1", via: "HKD"
          n_alternatives: 1
        P3:
          debug: false
          src: "G1BS", dst: "A2", send: "10/HKD/A2", via: "HKD"
          alternatives: [
            amount: "10/HKD/G1BS",
            paths: [["HKD/M1|M1", "HKD/G2SW|G2SW"]]
          ]
        P5:
          debug: false
          src: "M1",
          send: "10/HKD/M1",
          dst: "G1BS",
          via: "HKD"
        P4:
          debug: false
          src: "G2SW", send: "10/HKD/A1", dst: "A1", via: "HKD"
          alternatives: [
            amount: "10/HKD/G2SW",
            paths: [["HKD/M1|M1", "HKD/G1BS|G1BS"]]
          ]
    }
  "Path Tests #4 (non-XRP to non-XRP, same currency)": {
    ledger:
      accounts:
        G1: balance: ["1000.0"]
        G2: balance: ["1000.0"]
        G3: balance: ["1000.0"]
        G4: balance: ["1000.0"]
        A1:
          balance: ["1000.0", "1000/HKD/G1"]
          trusts:  ["2000/HKD/G1"]
        A2:
          balance: ["1000.0", "1000/HKD/G2"]
          trusts:  ["2000/HKD/G2"]
        A3:
          balance: ["1000.0", "1000/HKD/G1"]
          trusts:  ["2000/HKD/G1"]
        A4:
          balance: ["10000.0"]
        M1:
          balance: ["11000.0", "1200/HKD/G1", "5000/HKD/G2"]
          trusts:  ["100000/HKD/G1", "100000/HKD/G2"]
          offers:  [
            ["1000/HKD/G1", "1000/HKD/G2"]
          ]
        M2:
          balance: ["11000.0", "1200/HKD/G1", "5000/HKD/G2"]
          trusts:  ["100000/HKD/G1", "100000/HKD/G2"]
          offers:  [
            ["10000.0", "1000/HKD/G2"]
            ["1000/HKD/G1", "10000.0"]
          ]

    paths_expected: {
      T4:
        "A) Borrow or repay":
          comment: 'Source -> Destination (repay source issuer)'
          src: "A1", send: "10/HKD/G1", dst: "G1", via: "HKD"
          alternatives: [amount: "10/HKD/A1", paths: []]

        "A2) Borrow or repay":
          comment: 'Source -> Destination (repay destination issuer)'
          src: "A1", send: "10/HKD/A1", dst: "G1", via: "HKD"
          alternatives: [amount: "10/HKD/A1", paths: []]

        "B) Common gateway":
          comment: 'Source -> AC -> Destination'
          src: "A1", send: "10/HKD/A3", dst: "A3", via: "HKD"
          alternatives: [amount: "10/HKD/A1", paths: [["HKD/G1|G1"]]]

        "C) Gateway to gateway":
          comment: 'Source -> OB -> Destination'
          src: "G1", send: "10/HKD/G2", dst: "G2", via: "HKD"
          debug: false
          alternatives: [
            amount: "10/HKD/G1"
            paths: [["HKD/M2|M2"],
                    ["HKD/M1|M1"],
                    ["HKD/G2|$"]
                    ["XRP|$", "HKD/G2|$"]
                  ]
          ]

        "D) User to unlinked gateway via order book":
          comment: 'Source -> AC -> OB -> Destination'
          src: "A1", send: "10/HKD/G2", dst: "G2", via: "HKD"
          debug: false
          alternatives: [
            amount: "10/HKD/A1"
            paths: [
              ["HKD/G1|G1", "HKD/G2|$"],                                   # <--
              ["HKD/G1|G1", "HKD/M2|M2"],
              ["HKD/G1|G1", "HKD/M1|M1"],
              ["HKD/G1|G1", "XRP|$", "HKD/G2|$"]
            ]
          ]

        "I4) XRP bridge":
          comment: 'Source -> AC -> OB to XRP -> OB from XRP -> AC -> Destination'
          src: "A1", send: "10/HKD/A2", dst: "A2", via: "HKD"
          debug: false
          alternatives: [
            amount: "10/HKD/A1",
            paths: [
              # Focus
              ["HKD/G1|G1", "HKD/G2|$", "HKD/G2|G2"            ],
              ["HKD/G1|G1", "XRP|$",    "HKD/G2|$", "HKD/G2|G2"],          # <--
              # Incidental
              ["HKD/G1|G1", "HKD/M1|M1", "HKD/G2|G2"],
              ["HKD/G1|G1", "HKD/M2|M2", "HKD/G2|G2"]
            ]
          ]

    }
  },
  "Path Tests #2 (non-XRP to non-XRP, same currency)": {
    ledger:
      accounts:
        G1: balance: ["1000.0"]
        G2: balance: ["1000.0"]
        A1:
          balance: ["1000.0", "1000/HKD/G1"]
          trusts:  ["2000/HKD/G1"]
        A2:
          balance: ["1000.0", "1000/HKD/G2"]
          trusts:  ["2000/HKD/G2"]
        A3:
          balance: ["1000.0"]
          trusts:  ["2000/HKD/A2"]
        M1:
          balance: ["11000.0", "5000/HKD/G1", "5000/HKD/G2"]
          trusts:  ["100000/HKD/G1", "100000/HKD/G2"]
          offers:  [
            ["1000/HKD/G1",    "1000/HKD/G2"]
            # ["2000/HKD/G2",  "2000/HKD/G1"]
            # ["2000/HKD/M1",  "2000/HKD/G1"]
            # ["100.0",        "1000/HKD/G1"]
            # ["1000/HKD/G1",        "100.0"]
          ]

    paths_expected: {
      T4:
        "E) Gateway to user":
          ledger: false
          comment: 'Source -> OB -> AC -> Destination'
          # comment: 'Gateway -> OB -> Gateway 2 -> User'
          src: "G1", send: "10/HKD/A2", dst: "A2", via: "HKD"
          debug: false
          alternatives: [
            amount: "10/HKD/G1"
            paths: [
              ["HKD/G2|$", "HKD/G2|G2"],
              ["HKD/M1|M1", "HKD/G2|G2"]
            ]
          ]

        "F) Different gateways, ripple  _skip":
          comment: 'Source -> AC -> AC -> Destination'

        "G) Different users of different gateways, ripple  _skip":
          comment: 'Source -> AC -> AC -> AC -> Destination'

        "H) Different gateways, order book  _skip":
          comment: 'Source -> AC -> OB -> AC -> Destination'

        "I1) XRP bridge  _skip":
          comment: 'Source -> OB to XRP -> OB from XRP -> Destination'
          src: "A4", send: "10/HKD/G2", dst: "G2", via: "XRP"
          debug: true

        "I2) XRP bridge  _skip":
          comment: 'Source -> AC -> OB to XRP -> OB from XRP -> Destination'

        "I3) XRP bridge  _skip":
          comment: 'Source -> OB to XRP -> OB from XRP -> AC -> Destination'
    }
  }

################################# DEFINE SUITES ################################

define_suites(path_finding_cases)
