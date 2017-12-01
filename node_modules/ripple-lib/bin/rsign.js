#!/usr/bin/env node
/* eslint-disable no-var */
'use strict';
var Transaction = require('..').Transaction;

function read_input(callback) {
  var stdin = '';
  process.stdin.on('data', function(data) {
    stdin += data;
  });
  process.stdin.on('end', function() {
    callback(stdin);
  });
  process.stdin.resume();
}

function print_usage() {
  console.log(
    'Usage: rsign.js <secret> <json>\n\n',
    'Example: rsign.js ssq55ueDob4yV3kPVnNQLHB6icwpC', '\'' +
    JSON.stringify({
      TransactionType: 'Payment',
      Account: 'r3P9vH81KBayazSTrQj6S25jW6kDb779Gi',
      Destination: 'r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV',
      Amount: '200000000',
      Fee: '10',
      Sequence: 1
    }) + '\''
  );
}

function sign_transaction(tx_json_object, secret, verbose) {
  var tx = new Transaction();

  tx.tx_json = tx_json_object;
  tx._secret = secret;
  tx.complete();

  var unsigned_blob = tx.serialize().to_hex();
  var unsigned_hash = tx.signingHash();
  tx.sign();

  if (verbose) {
    var sim = { };
    sim.tx_blob = tx.serialize().to_hex();
    sim.tx_json = tx.tx_json;
    sim.tx_signing_hash = unsigned_hash;
    sim.tx_unsigned = unsigned_blob;
    console.log(JSON.stringify(sim, null, 2));
  } else {
    console.log(tx.serialize().to_hex());
  }
}

function ready(tx_json, secret, verbose) {
  if (!(tx_json && secret)) {
    console.error('Invalid arguments\n');
    print_usage();
    return;
  }

  var tx_json_object;
  try {
    tx_json_object = JSON.parse(tx_json);
  } catch(exception) {
    console.error('Invalid JSON\n');
    print_usage();
    return;
  }
  sign_transaction(tx_json_object, secret, verbose);
}

function main() {
  var argv = process.argv.slice(2);
  var verbose;
  var secret;
  var tx_json;

  if (~argv.indexOf('-v')) {
    argv.splice(argv.indexOf('-v'), 1);
    verbose = true;
  }

  secret = argv.shift();
  tx_json = argv.shift();

  if (tx_json === '-') {
    read_input(function(stdin) {
      ready(stdin, secret, verbose);
    });
  } else {
    ready(tx_json, secret, verbose);
  }
}

main();
// vim:sw=2:sts=2:ts=8:et
