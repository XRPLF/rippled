module.exports = {
  "Partially crossed completely via bridging": {

    "pre_ledger": {"accounts": {"G1": {"balance": ["1000.0"]},
                                "G2": {"balance": ["1000.0"]},
                                "alice": {"balance": ["500000.0", "200/USD/G1"],
                                          "offers": [["100/USD/G1", "88.0"]]},
                                "bob": {"balance": ["500000.0", "500/USD/G2"],
                                        "offers": [["88.0", "100/USD/G2"]]},
                                "takerJoe": {"balance": ["500000.0", "500/USD/G1"]}}},

    "offer": ["takerJoe", "500/USD/G2", "500/USD/G1"],

    "post_ledger": {"accounts": {"takerJoe": {"balance": ["400/USD/G1", "100/USD/G2"],
                                              "offers": [["400/USD/G2", "400/USD/G1"]]}}}
  },

  "Partially crossed (halt)": {
    "pre_ledger": {"accounts": {"G1": {"balance": ["1000.0"]},
                                "G2": {"balance": ["1000.0"]},
                                "alice": {"balance": ["500000.0", "200/USD/G1"],
                                          "offers": [["100/USD/G1", "88.0"], ["100/USD/G1", "88.0"]]},
                                                //----/ (2)            (3)      (5) there's no offers left. Halt
                                "bob": {"balance": ["500000.0", "500/USD/G2"],
                                        "offers": [["88.0", "100/USD/G2"]]},
                                                //                  (4)
                                "takerJoe": {"balance": ["500000.0", "500/USD/G1"]}}},
                                           // (1)
    "offer": ["takerJoe", "500/USD/G2", "500/USD/G1"],

                                                // 500,000-88   200+100/USD/G1
    "post_ledger": {"accounts": {"alice": {"balance": ["499912.0", "300/USD/G1"],
                                           "offers": [["100/USD/G1", "88.0"]]},

                                  "bob": {"balance": ["500088.0", "400/USD/G2"],
                                          "offers": [/*["88.0", "100/USD/G2"]*/]},

                                 "takerJoe": {"balance": ["100/USD/G2", "400/USD/G1"],
                                              "offers": [["400/USD/G2", "400/USD/G1"]]}}}
  },

  "Partially crossed completely via bridging (Sell)": {

  "pre_ledger": {"accounts": {"G1": {"balance": ["1000.0"]},
                             "G2": {"balance": ["1000.0"]},
                             "alice": {"balance": ["500000.0", "200/USD/G1"],
                                       "offers": [["200/USD/G1", "176.0", "Sell"]]},
                             "bob": {"balance": ["500000.0", "500/USD/G2"],
                                     "offers": [["88.0", "100/USD/G2"]]},
                             "takerJoe": {"balance": ["500000.0", "500/USD/G1"]}}},

  "offer": ["takerJoe", "500/USD/G2", "500/USD/G1", "Sell"],

  "post_ledger": {"accounts": {"alice": {"balance": ["499912.0", "299.9999999999999/USD/G1"],
                                        "offers": [["100.0000000000001/USD/G1", "88.0"]]},
                              "takerJoe": {"balance": ["100/USD/G2", "400.0000000000001/USD/G1"],
                                           "offers": [["400.0000000000001/USD/G2", "400.0000000000001/USD/G1"]]}}}
  },

  "Completely crossed via bridging + direct": {

    "pre_ledger": {"accounts": {"G1": {"balance": ["1000.0"]},
                                "G2": {"balance": ["1000.0"]},
                                "alice": {"balance": ["500000.0", "500/USD/G1", "500/USD/G2"],
                                          "offers": [["50/USD/G1", "50/USD/G2"],
                                                     ["49/USD/G1", "50/USD/G2"],
                                                     ["48/USD/G1", "50/USD/G2"],
                                                     ["47/USD/G1", "50/USD/G2"],
                                                     ["46/USD/G1", "50/USD/G2"],
                                                     ["45/USD/G1", "50/USD/G2"],
                                                     ["44/USD/G1", "50/USD/G2"],
                                                     ["43/USD/G1", "50/USD/G2"],
                                                     ["100/USD/G1", "88.0"]]},
                                "bob": {"balance": ["500000.0", "500/USD/G2"],
                                        "offers": [["88.0", "100/USD/G2"]]},
                                "takerJoe": {"balance": ["500000.0", "600/USD/G1"]}}},

    "offer": ["takerJoe", "500/USD/G2", "500/USD/G1"],

    "post_ledger": {"accounts": {"takerJoe": {"balance": ["500/USD/G2", "128/USD/G1"]}}}
  },

  "Partially crossed via bridging + direct": {
    "pre_ledger": {"accounts": {"G1": {"balance": ["1000.0"]},
                                "G2": {"balance": ["1000.0"]},
                                "alice": {"balance": ["500000.0", "500/USD/G1", "500/USD/G2"],
                                          "offers": [["372/USD/G1", "400/USD/G2"],
                                                     ["100/USD/G1", "88.0"]]},
                                "bob": {"balance": ["500000.0", "500/USD/G2"],
                                        "offers": [["88.0", "100/USD/G2"]]},
                                "takerJoe": {"balance": ["500000.0", "600/USD/G1"]}}},

    "offer": ["takerJoe", "600/USD/G2", "600/USD/G1"],

    "post_ledger": {"accounts": {"takerJoe": {"balance": ["500/USD/G2", "128/USD/G1"],
                                              "offers": [["100/USD/G2", "100/USD/G1"]]}}}
  },

  "Partially crossed via bridging + direct 2": {
    "pre_ledger": {"accounts": {"G1": {"balance": ["1000.0"]},
                                "G2": {"balance": ["1000.0"]},
                                "alice": {"balance": ["500000.0", "500/USD/G1", "500/USD/G2"],
                                          "offers": [["372/USD/G1", "400/USD/G2"],
                                                     ["100/USD/G1", "88.0"]]},
                                "bob": {"balance": ["500000.0", "500/USD/G2"],
                                        "offers": [["88.0", "100/USD/G2"]]},
                                "takerJoe": {"balance": ["500000.0", "600/USD/G1"]}}},

    "offer": ["takerJoe", "600/USD/G2", "600/USD/G1", "Sell"],

    "post_ledger": {"accounts": {"takerJoe": {"balance": ["500/USD/G2", "128/USD/G1"],
                                              "offers": [["128/USD/G2", "128/USD/G1"]]}}}
  }
}