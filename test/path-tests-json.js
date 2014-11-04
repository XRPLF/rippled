module.exports = {

  // Suffixing this suite name with _only will run ONLY this test
  // "Path Tests #6: Two orderbooks _only": {

  // Suffixing this suite name with _skip will skip the test
  // "Path Tests #6: Two orderbooks _skip": {

  "Path Tests #6: Two orderbooks": {
    // Maybe don't want to spawn the server, as we want to run the server under
    // gdb or some other debugger.
    // We can run `build/rippled -a --conf tmp/server/alpha/rippled.cfg`
    no_server: false,

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
        "E) user to gateway to gateyway to user": {
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

              paths: [[
                // Through GW1
                "FOO/GW1|GW1",
                // Through Orderbook
                "FOO/GW2|$",
                // Through Orderbook
                "FOO/GW3|$"
              ]]
            }]
        }
      }
    }
  },

  "Path Tests #1 (XRP -> XRP) and #2 (XRP -> IOU)": {

    "ledger": {"accounts": {"A1": {"balance": ["100000.0",
                                               "3500/XYZ/G1",
                                               "1200/ABC/G3"],
                                   "trusts": ["5000/XYZ/G1",
                                              "5000/ABC/G3"]},
                            "A2": {"balance": ["10000.0"],
                                   "trusts": ["5000/XYZ/G2",
                                              "5000/ABC/G3"]},
                            "A3": {"balance": ["1000.0"],
                                   "trusts": ["1000/ABC/A2"]},
                            "G1": {"balance": ["1000.0"]},
                            "G2": {"balance": ["1000.0"]},
                            "G3": {"balance": ["1000.0"]},
                            "M1": {"balance": ["1000.0",
                                               "25000/XYZ/G2",
                                               "25000/ABC/G3"],
                                   "offers": [["1000/XYZ/G1",
                                               "1000/XYZ/G2"],
                                              ["10000.0",
                                               "1000/ABC/G3"]],
                                   "trusts": ["100000/XYZ/G1",
                                              "100000/ABC/G3",
                                              "100000/XYZ/G2"]}}},

    "paths_expected": {"T1": {"A1": {"n_alternatives": 0,
                                     "src": "A1",
                                     "send": "10.0",
                                     "dst": "A2",
                                     "via": "XRP"},
                              "A2": {"comment": "Send to non existing account",
                                     "src": "A1",
                                     "send_comment": "malformed error not great for 10.0 amount",
                                     "send": "200.0",
                                     "dst": "rBmhuVAvi372AerwzwERGjhLjqkMmAwxX",
                                     "via": "XRP",
                                     "n_alternatives": 0}},
                       "T2": {"A": {"alternatives": [{"amount": "100.0",
                                                           "paths": [
                                                            ["ABC/G3|$"]
                                                           ]}],
                                    "src": "A2",
                                    "send": "10/ABC/G3",
                                    "dst": "G3",
                                    "via": "XRP",
                                    "debug": 0,
                                    "n_alternatives": 1},
                              "B": {"alternatives": [{"amount": "10.0",
                                                      "paths": [["ABC/G3|$",
                                                                 "ABC/G3|G3"]]}],
                                    "src": "A1",
                                    "send": "1/ABC/A2",
                                    "dst": "A2",
                                    "via": "XRP",
                                    "n_alternatives": 1},
                              "C": {"alternatives": [{"amount": "10.0",
                                                      "paths": [["ABC/G3|$",
                                                                 "ABC/G3|G3",
                                                                 "ABC/A2|A2"]]}],
                                    "src": "A1",
                                    "send": "1/ABC/A3",
                                    "dst": "A3",
                                    "via": "XRP",
                                    "n_alternatives": 1}}}},
 "Path Tests #3 (non-XRP to XRP)": {

    "ledger": {"accounts": {"A1": {"balance": ["1000.0",
                                               "1000/ABC/G3"]},
                            "A2": {"balance": ["1000.0",
                                               "1000/ABC/G3"]},
                            "G3": {"balance": ["1000.0"]},
                            "M1": {"balance": ["11000.0",
                                               "1200/ABC/G3"],
                                   "offers": [["1000/ABC/G3",
                                               "10000.0"]],
                                   "trusts": ["100000/ABC/G3"]}}},

    "paths_expected": {"T3": {"A": {"alternatives": [{"amount": "1/ABC/A1",
                                                      "paths": [["ABC/G3|G3",
                                                                 "XRP|$"]]}],
                                    "src": "A1",
                                    "dst": "A2",
                                    "debug":false,
                                    "send": "10.0",
                                    "via": "ABC"}}}}
}
