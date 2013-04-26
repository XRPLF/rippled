#!/usr/bin/node

var Transaction = require('ripple-lib').Transaction;

var cursor      = 2;
var verbose;
var secret;
var tx_json;

var usage = function () {
  console.log(
    "Usage: rsign.js <secret> <json>\n"
    + "  Example: rsign.js ssq55ueDob4yV3kPVnNQLHB6icwpC '{ \"TransactionType\" : \"Payment\", \"Account\" : \"r3P9vH81KBayazSTrQj6S25jW6kDb779Gi\", \"Destination\" : \"r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV\", \"Amount\" : \"200000000\", \"Fee\" : \"10\", \"Sequence\" : \"1\" }'"
    );
};

if (process.argv.length > cursor && process.argv[cursor] === "-v")
{
  verbose = true;
  cursor++;
}

if (process.argv.length > cursor)
{
  secret = process.argv[cursor++];
}

if (process.argv.length > cursor)
{
  tx_json = JSON.parse(process.argv[cursor++]);
}

if (process.argv.length !== cursor || !secret || !tx_json)
{
  usage();
}
else
{
    var tx = new Transaction();

    tx.tx_json        = tx_json;
    tx._secret        = secret;
    tx.complete();

    var unsigned      = tx.serialize().to_hex();
    tx.sign();

    if (verbose)
    {
      var sim = {};

      sim.tx_blob         = tx.serialize().to_hex();
      sim.tx_json         = tx.tx_json;
      sim.tx_signing_hash = tx.signing_hash().to_hex();
      sim.tx_unsigned     = unsigned;

      console.log(JSON.stringify(sim, undefined, 2));
    }
    else
    {
      console.log(tx.serialize().to_hex());
    }
}

// vim:sw=2:sts=2:ts=8:et
