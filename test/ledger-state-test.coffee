################################################################################

async                      = require 'async'
simple_assert              = require 'assert'
deep_eq                    = require 'deep-equal'
testutils                  = require './testutils'

{
  LedgerVerifier
  Balance
}                          = require './ledger-state'

#################################### CONFIG ####################################

config = testutils.init_config()

#################################### HELPERS ###################################

assert = simple_assert
prettyj = pretty_json =  (v) -> JSON.stringify(v, undefined, 2)

describe 'Balance', ->
  it 'parses native balances', ->
    bal = new Balance("1.000")
    assert.equal bal.is_native, true
    assert.equal bal.limit, null

  it 'parses iou balances', ->
    bal = new Balance("1.000/USD/bob")
    assert.equal bal.is_native, false
    assert.equal bal.limit, null
    assert.equal bal.amount.currency().to_json(), 'USD'

  it 'parses iou balances with limits', ->
    bal = new Balance("1-500/USD/bob")
    assert.equal bal.is_native, false
    assert.equal bal.amount.currency().to_json(), 'USD'
    assert.equal bal.limit.to_json().value, '500'
    assert.equal bal.amount.to_json().value, '1'

describe 'LedgerVerifier', ->
  lv = null

  declaration=
    accounts:
      bob:
        balance: ['100.0', '200-500/USD/alice']
        offers: [['89.0', '100/USD/alice'], ['89.0', '100/USD/alice']]

  # We are using this because mocha and coffee-script is a retarded combination
  # unfortunately, which terminates the program silently upon any require time
  # exceptions. TODO: investigate obviously, but for the moment this is an
  # acceptable workaround.
  suiteSetup ->
    remote_dummy = {set_secret: (->)}
    lv = new LedgerVerifier(declaration, remote_dummy, config, assert)

  it 'tracks xrp balances', ->
    assert.equal lv.xrp_balances['bob'].to_json(), '100000000'

  it 'tracks iou balances', ->
    assert.equal lv.iou_balances['bob']['USD/alice'].to_json().value, '200'

  it 'tracks iou trust limits', ->
    assert.equal lv.trusts['bob']['USD/alice'].to_json().value, '500'

  it 'can verify', ->
    account_offers = [
      {
          "account": "bob",
          "offers": [
            {
              "flags": 65536,
              "seq": 2,
              "taker_gets": {
                "currency": "USD",
                "issuer": "alice",
                "value": "100"
              },
              "taker_pays": "88000000"
            }
          ]
        }
    ]

    account_lines = [{
      "account": "bob",
      "lines": [
        {
          "account": "alice",
          "balance": "201",
          "currency": "USD",
          "limit": "500",
          "limit_peer": "0",
          "quality_in": 0,
          "quality_out": 0
        },
      ]
    }]

    account_infos = [{
      "account_data": {
        "Account": "bob",
        "Balance": "999"+ "999"+ "970",
        "Flags": 0,
        "LedgerEntryType": "AccountRoot",
        "OwnerCount": 0,
        "PreviousTxnID": "3D7823B577A5AF5860273B3DD13CA82D072B63B3B095DE1784604A5B41D7DD1D",
        "PreviousTxnLgrSeq": 5,
        "Sequence": 3,
        "index": "59BEA57D1A27B6A560ECA226ABD10DE80C3ADC6961039908087ACDFA92F71489"
      },
      "ledger_current_index": 8
    }]

    errors = lv.verify account_infos, account_lines, account_offers

    assert.equal errors.bob.balance['USD/alice'].expected, '200'
    assert.equal errors.bob.balance['USD/alice'].actual, '201'

    assert.equal errors.bob.balance['XRP'].expected, '100'
    assert.equal errors.bob.balance['XRP'].actual, '999.99997'

    assert.equal errors.bob.offers[0].taker_pays.actual, '88/XRP'
    assert.equal errors.bob.offers[0].taker_pays.expected, '89/XRP'

    # {"expected":["89.0","100/USD/alice"],"actual":"missing"}
    assert.equal errors.bob.offers[1].actual, 'missing'

    expected = {
      "bob": {
        "balance": {
          "XRP": {
            "actual": "999.99997",
            "expected": "100"
          },
          "USD/alice": {
            "actual": "201",
            "expected": "200"
          }
        },
        "offers": [
          {
            "taker_pays": {
              "expected": "89/XRP",
              "actual": "88/XRP"
            }
          },
          "missing"
        ]
      }
    }
