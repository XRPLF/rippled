#!/usr/bin/env node
/* eslint-disable no-var */
'use strict';
var SerializedObject = require('..').SerializedObject;

function main() {
  var argv = process.argv.slice(2);
  var blob = argv.shift();

  if (blob === '-') {
    read_input(ready);
  } else {
    ready(blob);
  }
}

function read_input(callback) {
  var tx_json = '';
  process.stdin.on('data', function(data) {
    tx_json += data;
  });
  process.stdin.on('end', callback);
  process.stdin.resume();
}

function ready(blob) {
  var valid_arguments = blob;

  if (!valid_arguments) {
    console.error('Invalid arguments\n');
    print_usage();
  } else {
    decode(blob);
  }
}

function print_usage() {
  /* eslint-disable max-len */
  console.log(
    'Usage: decode_binary.js <hex_blob>\n\n',
    'Example: decode_binary.js 120000240000000161D6871AFD498D00000000000000000000000000005553440000000000550FC62003E785DC231A1058A05E56E3F09CF4E668400000000000000A732102AE75B908F0A95F740A7BFA96057637E5C2170BC8DAD13B2F7B52AE75FAEBEFCF811450F97A072F1C4357F1AD84566A609479D927C9428314550FC62003E785DC231A1058A05E56E3F09CF4E6'
  );
  /* eslint-enable max-len */
}

function decode(blob) {
  var buffer = new SerializedObject(blob);
  console.log(buffer.to_json());
}

main();
// vim:sw=2:sts=2:ts=8:et
