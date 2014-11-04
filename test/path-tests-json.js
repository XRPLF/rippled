module.exports = {

  // Suffixing this suite name with _only will run ONLY this test
  // "Path Tests #6: Two orderbooks _only": {

  // Suffixing this suite name with _skip will skip the test
  // "Path Tests #6: Two orderbooks _skip": {

  "Path Tests #6: Two orderbooks (RIPD-639)": {
    // Maybe don't want to spawn the server, as we want to run the server under
    // gdb or some other debugger.
    // We can run `build/rippled -a --conf tmp/server/alpha/rippled.cfg`
    no_server: false,
    // warning: console.warn("This test is up the wazoo"),

    ledger: {
      // Set path to dump a full ledger after setup via `ledger` rpc request
      // Includes this structure plus a realiased accountState (as well as
      // unmodified).
      // dump_ledger: "/some/path/ledger-dump.json",

      // This will dump an equivalent rpc script, which will do the setting up.
      // dump_setup_script: "/some/path/some-script.sh",

      accounts: {
        Alice: {
          balance: ["1000.0", "1/FOO/GW1"],
          // We only need to specify the trust if we need a trust greater
          // than the balance.
          // trusts: ["1/FOO/GW1"]
        },

        Mark: {
          balance: ["1000.0", "1/FOO/GW2", "1/FOO/GW3"],
          // We only need to specify the trust if we need a trust greater
          // than the balance.
          // trusts: ["1/FOO/GW2", "1/FOO/GW3"],

                  // Pays          Gets
          offers: [["1/FOO/GW1", "1/FOO/GW2"],
                   ["1/FOO/GW2", "1/FOO/GW3"]]
        },

        GW1: {balance: ["1000.0"]},
        GW2: {balance: ["1000.0"]},
        GW3: {balance: ["1000.0"]}
      }
    },
    paths_expected: {
      A1: {
        "E) user to gateway to gateyway to user (works) _skip": {
          comment: 'Source -> OB -> OB -> Destination',
          src: "Alice",
          // destination_amount
          send: "1/FOO/GW3",
          // destination account
          dst: "GW3",
          // specify as source currency (non optional)
          via: "FOO",
          // will dump more information
          debug: false,
          alternatives: [
            {
              // source_amount
              amount: "1/FOO/Alice",
              paths: [
                [
                // Through GW1
                "FOO/GW1|GW1",
                // Through Orderbook
                "FOO/GW2|$",
                // Through Orderbook
                "FOO/GW3|$"
                ]
              ]
            }
            ]
        },
        "E) user to gateway to gateyway to user (is broken)": {
          src: "Alice",
          send: "1/FOO/GW3",
          dst: "GW3",
          via: "FOO",
          debug: false,
          alternatives: []
        },
      }
    }
  },

  "Path Tests #1 (XRP -> XRP) and #2 (XRP -> IOU)": {

    "ledger": {
      "accounts":
        {"A1": {"balance": ["100000.0", "3500/XYZ/G1", "1200/ABC/G3"],
                "trusts": ["5000/XYZ/G1", "5000/ABC/G3"]},
         "A2": {"balance": ["10000.0"], "trusts": ["5000/XYZ/G2", "5000/ABC/G3"]},
         "A3": {"balance": ["1000.0"], "trusts": ["1000/ABC/A2"]},
         "G1": {"balance": ["1000.0"]},
         "G2": {"balance": ["1000.0"]},
         "G3": {"balance": ["1000.0"]},
         "M1": {"balance": ["1000.0", "25000/XYZ/G2", "25000/ABC/G3"],
                "offers": [["1000/XYZ/G1", "1000/XYZ/G2"],
                           ["10000.0", "1000/ABC/G3"]],
                "trusts": ["100000/XYZ/G1", "100000/ABC/G3", "100000/XYZ/G2"]}}},

    "paths_expected": {
      "T1": {"A1": {"dst": "A2",
                     "n_alternatives": 0,
                     "send": "10.0",
                     "src": "A1",
                     "via": "XRP"},
              "A2": {"comment": "Send to non existing account",
                     "dst": "rBmhuVAvi372AerwzwERGjhLjqkMmAwxX",
                     "n_alternatives": 0,
                     "send": "200.0",
                     "send_comment": "malformed error not great for 10.0 amount",
                     "src": "A1",
                     "via": "XRP"}},
       "T2": {"A": {"alternatives": [{"amount": "100.0", "paths": [["ABC/G3|$"]]}],
                    "debug": 0,
                    "dst": "G3",
                    "n_alternatives": 1,
                    "send": "10/ABC/G3",
                    "src": "A2",
                    "via": "XRP"},
              "B": {"alternatives": [{"amount": "10.0",
                                      "paths": [["ABC/G3|$", "ABC/G3|G3"]]}],
                    "dst": "A2",
                    "n_alternatives": 1,
                    "send": "1/ABC/A2",
                    "src": "A1",
                    "via": "XRP"},
              "C": {"alternatives": [{"amount": "10.0",
                                      "paths": [["ABC/G3|$",
                                                 "ABC/G3|G3",
                                                 "ABC/A2|A2"]]}],
                    "dst": "A3",
                    "n_alternatives": 1,
                    "send": "1/ABC/A3",
                    "src": "A1",
                    "via": "XRP"}}}},

 "Path Tests #3 (non-XRP to XRP)": {

    "ledger": {
       "accounts": {"A1": {"balance": ["1000.0", "1000/ABC/G3"]},
                    "A2": {"balance": ["1000.0", "1000/ABC/G3"]},
                    "G3": {"balance": ["1000.0"]},
                    "M1": {"balance": ["11000.0", "1200/ABC/G3"],
                           "offers": [["1000/ABC/G3", "10000.0"]],
                           "trusts": ["100000/ABC/G3"]}}},

    "paths_expected": {
      "T3": {
        "A": {"alternatives": [{"amount": "1/ABC/G3", "paths": []}],
               "debug": false,
               "dst": "A2",
               "send": "10.0",
               "src": "A1",
               "via": ["ABC/G3"]},

         "B": {"alternatives": [{"amount": "1/ABC/G3", "paths": []},
                                {"amount": "1/ABC/A1",
                                 "paths": [["ABC/G3|G3", "XRP|$"]]}],
               "debug": false,
               "dst": "A2",
               "send": "10.0",
               "src": "A1",
               "via": ["ABC/G3", "ABC/A1"]}}},
  },

  "CNY test":{
    "ledger": {
      "accounts": {
        "A1": {"balance": ["1240.997150",
                            "0.0000000119761/CNY/MONEY_MAKER_1",
                            "33.047994/CNY/GATEWAY_DST"],
                "offers": [],
                "trusts": ["1000000/CNY/MONEY_MAKER_1",
                           "100000/USD/MONEY_MAKER_1",
                           "10000/BTC/MONEY_MAKER_1",
                           "1000/USD/GATEWAY_DST",
                           "1000/CNY/GATEWAY_DST"]},

         "A2": {"balance": ["14115.046893",
                            "209.3081873019994/CNY/MONEY_MAKER_1",
                            "694.6251706504019/CNY/GATEWAY_DST"],
                "offers": [["2000000000", "66.8/CNY/MONEY_MAKER_1"],
                           ["1200000000", "42/CNY/GATEWAY_DST"],
                           ["43.2/CNY/MONEY_MAKER_1", "900000000"]],
                "trusts": ["3000/CNY/MONEY_MAKER_1", "3000/CNY/GATEWAY_DST"]},

         "A3": {"balance": ["512087.883181",
                            "23.617050013581/CNY/MONEY_MAKER_1",
                            "70.999614649799/CNY/GATEWAY_DST"],
                "offers": [["2240/CNY/MONEY_MAKER_1", "50000000000"]],
                "trusts": ["10000/CNY/MONEY_MAKER_1", "10000/CNY/GATEWAY_DST"]},

         "GATEWAY_DST": {"balance": ["10846.168060"], "offers": [], "trusts": []},

         "MONEY_MAKER_1": {"balance": ["4291.430036"], "offers": [], "trusts": []},

         "MONEY_MAKER_2": {"balance": ["106839375770",
                                       "0.0000000003599/CNY/MONEY_MAKER_1",
                                       "137.6852546843001/CNY/GATEWAY_DST"],
                           "offers": [["1000000", "1/CNY/GATEWAY_DST"],
                                      ["1/CNY/GATEWAY_DST", "1000000"],
                                      ["318000/CNY/GATEWAY_DST", "53000000000"],
                                      ["209000000", "4.18/CNY/MONEY_MAKER_2"],
                                      ["990000/CNY/MONEY_MAKER_1", "10000000000"],
                                      ["9990000/CNY/MONEY_MAKER_1", "10000000000"],
                                      ["8870000/CNY/GATEWAY_DST", "10000000000"],
                                      ["232000000", "5.568/CNY/MONEY_MAKER_2"]],
                           "trusts": ["1001/CNY/MONEY_MAKER_1",
                                      "1001/CNY/GATEWAY_DST"]},

         "SRC": {"balance": ["4999.999898"], "offers": [], "trusts": []}}},

    "paths_expected": {
      "BS": {"P101": {"dst": "GATEWAY_DST",
                       "n_alternatives": 1,
                       "send": "10.1/CNY/GATEWAY_DST",
                       "src": "SRC",
                       "via": "XRP"}}}},

  "Path Tests (Bitstamp + SnapSwap account holders | liquidity provider with no offers)": {
    "ledger": {
      "accounts": {"A1": {"balance": ["1000.0", "1000/HKD/G1BS"],
                           "trusts": ["2000/HKD/G1BS"]},
                    "A2": {"balance": ["1000.0", "1000/HKD/G2SW"],
                           "trusts": ["2000/HKD/G2SW"]},
                    "G1BS": {"balance": ["1000.0"]},
                    "G2SW": {"balance": ["1000.0"]},
                    "M1": {"balance": ["11000.0",
                                       "1200/HKD/G1BS",
                                       "5000/HKD/G2SW"],
                           "trusts": ["100000/HKD/G1BS", "100000/HKD/G2SW"]}}},
    "paths_expected": {
      "BS": {
        "P1": {"debug": false,
               "dst": "A2",
               "n_alternatives": 1,
               "send": "10/HKD/A2",
               "src": "A1",
               "via": "HKD"},
        "P2": {"debug": false,
               "dst": "A1",
               "n_alternatives": 1,
               "send": "10/HKD/A1",
               "src": "A2",
               "via": "HKD"},
        "P3": {"alternatives": [{"amount": "10/HKD/G1BS",
                                 "paths": [["HKD/M1|M1", "HKD/G2SW|G2SW"]]}],
               "debug": false,
               "dst": "A2",
               "send": "10/HKD/A2",
               "src": "G1BS",
               "via": "HKD"},
        "P4": {"alternatives": [{"amount": "10/HKD/G2SW",
                                 "paths": [["HKD/M1|M1", "HKD/G1BS|G1BS"]]}],
               "debug": false,
               "dst": "A1",
               "send": "10/HKD/A1",
               "src": "G2SW",
               "via": "HKD"},
        "P5": {"debug": false,
               "dst": "G1BS",
               "send": "10/HKD/M1",
               "src": "M1",
               "via": "HKD"}}}},

  "Path Tests #4 (non-XRP to non-XRP, same currency)": {
    "ledger": {
      "accounts": {
        "A1": {"balance": ["1000.0", "1000/HKD/G1"], "trusts": ["2000/HKD/G1"]},
         "A2": {"balance": ["1000.0", "1000/HKD/G2"], "trusts": ["2000/HKD/G2"]},
         "A3": {"balance": ["1000.0", "1000/HKD/G1"], "trusts": ["2000/HKD/G1"]},
         "A4": {"balance": ["10000.0"]},
         "G1": {"balance": ["1000.0"]},
         "G2": {"balance": ["1000.0"]},
         "G3": {"balance": ["1000.0"]},
         "G4": {"balance": ["1000.0"]},
         "M1": {"balance": ["11000.0", "1200/HKD/G1", "5000/HKD/G2"],
                "offers": [["1000/HKD/G1", "1000/HKD/G2"]],
                "trusts": ["100000/HKD/G1", "100000/HKD/G2"]},
         "M2": {"balance": ["11000.0", "1200/HKD/G1", "5000/HKD/G2"],
                "offers": [["10000.0", "1000/HKD/G2"], ["1000/HKD/G1", "10000.0"]],
                "trusts": ["100000/HKD/G1", "100000/HKD/G2"]}}},

    "paths_expected": {
      "T4": {
        "A) Borrow or repay": {
          "alternatives": [{"amount": "10/HKD/A1", "paths": []}],
          "comment": "Source -> Destination (repay source issuer)",
          "dst": "G1",
          "send": "10/HKD/G1",
          "src": "A1",
          "via": "HKD"},

        "A2) Borrow or repay": {
          "alternatives": [{"amount": "10/HKD/A1", "paths": []}],
          "comment": "Source -> Destination (repay destination issuer)",
          "dst": "G1",
          "send": "10/HKD/A1",
          "src": "A1",
          "via": "HKD"},

        "B) Common gateway": {
          "alternatives": [{"amount": "10/HKD/A1", "paths": [["HKD/G1|G1"]]}],
          "comment": "Source -> AC -> Destination",
          "dst": "A3",
          "send": "10/HKD/A3",
          "src": "A1",
          "via": "HKD"},

        "C) Gateway to gateway": {
           "alternatives": [{"amount": "10/HKD/G1",
                             "paths": [["HKD/M2|M2"],
                                       ["HKD/M1|M1"],
                                       ["HKD/G2|$"],
                                       ["XRP|$", "HKD/G2|$"]]}],
           "comment": "Source -> OB -> Destination",
           "debug": false,
           "dst": "G2",
           "send": "10/HKD/G2",
           "src": "G1",
           "via": "HKD"},

        "D) User to unlinked gateway via order book": {
          "alternatives": [{"amount": "10/HKD/A1",
                            "paths": [["HKD/G1|G1", "HKD/G2|$"],
                                      ["HKD/G1|G1", "HKD/M2|M2"],
                                      ["HKD/G1|G1", "HKD/M1|M1"],
                                      ["HKD/G1|G1", "XRP|$", "HKD/G2|$"]]}],
          "comment": "Source -> AC -> OB -> Destination",
          "debug": false,
          "dst": "G2",
          "send": "10/HKD/G2",
          "src": "A1",
          "via": "HKD"},

        "I4) XRP bridge": {
          "alternatives": [{"amount": "10/HKD/A1",
                            "paths": [["HKD/G1|G1", "HKD/G2|$", "HKD/G2|G2"],
                                      ["HKD/G1|G1",
                                       "XRP|$",
                                       "HKD/G2|$",
                                       "HKD/G2|G2"],
                                      ["HKD/G1|G1", "HKD/M1|M1", "HKD/G2|G2"],
                                      ["HKD/G1|G1", "HKD/M2|M2", "HKD/G2|G2"]]}],
          "comment": "Source -> AC -> OB to XRP -> OB from XRP -> AC -> Destination",
          "debug": false,
          "dst": "A2",
          "send": "10/HKD/A2",
          "src": "A1",
          "via": "HKD"}}}},

  "Path Tests #2 (non-XRP to non-XRP, same currency)": {
    "ledger": {
      "accounts": {
        "A1": {"balance": ["1000.0", "1000/HKD/G1"], "trusts": ["2000/HKD/G1"]},
        "A2": {"balance": ["1000.0", "1000/HKD/G2"], "trusts": ["2000/HKD/G2"]},
        "A3": {"balance": ["1000.0"], "trusts": ["2000/HKD/A2"]},
        "G1": {"balance": ["1000.0"]},
        "G2": {"balance": ["1000.0"]},
        "M1": {"balance": ["11000.0", "5000/HKD/G1", "5000/HKD/G2"],
               "offers": [["1000/HKD/G1", "1000/HKD/G2"]],
               "trusts": ["100000/HKD/G1", "100000/HKD/G2"]}}},

    "paths_expected": {
      "T4": {
        "E) Gateway to user": {
            "alternatives": [{"amount": "10/HKD/G1",
                             "paths": [["HKD/G2|$", "HKD/G2|G2"],
                                       ["HKD/M1|M1", "HKD/G2|G2"]]}],
           "comment": "Source -> OB -> AC -> Destination",
           "debug": false,
           "dst": "A2",
           "ledger": false,
           "send": "10/HKD/A2",
           "src": "G1",
           "via": "HKD"},

        "F) Different gateways, ripple  _skip": {
          "comment": "Source -> AC -> AC -> Destination"},

        "G) Different users of different gateways, ripple  _skip": {
          "comment": "Source -> AC -> AC -> AC -> Destination"},

        "H) Different gateways, order book  _skip": {
          "comment": "Source -> AC -> OB -> AC -> Destination"},

        "I1) XRP bridge  _skip": {
            "comment": "Source -> OB to XRP -> OB from XRP -> Destination",
           "debug": true,
           "dst": "G2",
           "send": "10/HKD/G2",
           "src": "A4",
           "via": "XRP"},

        "I2) XRP bridge  _skip": {
          "comment": "Source -> AC -> OB to XRP -> OB from XRP -> Destination"},
        "I3) XRP bridge  _skip": {
          "comment": "Source -> OB to XRP -> OB from XRP -> AC -> Destination"}}}}
}
