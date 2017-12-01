#!/usr/bin/env node
/* eslint-disable no-var */
'use strict';
var UInt160 = require('..').UInt160;

function main() {
  var address = process.argv[2];

  if (address === '-') {
    readInput(validateAddress);
  } else {
    validateAddress(address);
  }
}

function readInput(callback) {
  var result = '';
  process.stdin.resume();
  process.stdin.setEncoding('utf8');
  process.stdin.on('data', function(data) {
    result += data;
  });
  process.stdin.on('end', function() {
    callback(result);
  });
}

function validateAddress(address) {
  process.stdout.write((UInt160.is_valid(address.trim()) ? '0' : '1') + '\r\n');
}

main();
