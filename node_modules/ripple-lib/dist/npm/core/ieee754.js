// Convert a JavaScript number to IEEE-754 Double Precision
// value represented as an array of 8 bytes (octets)
//
// Based on:
// http://cautionsingularityahead.blogspot.com/2010/04/javascript-and-ieee754-redux.html
//
// Found and modified from:
// https://gist.github.com/bartaz/1119041

'use strict';

var Float = exports.Float = {};

Float.toIEEE754 = function (v, ebits, fbits) {

  var bias = (1 << ebits - 1) - 1;

  // Compute sign, exponent, fraction
  var s, e, f;
  if (isNaN(v)) {
    e = (1 << bias) - 1;f = 1;s = 0;
  } else if (v === Infinity || v === -Infinity) {
    e = (1 << bias) - 1;f = 0;s = v < 0 ? 1 : 0;
  } else if (v === 0) {
    e = 0;f = 0;s = 1 / v === -Infinity ? 1 : 0;
  } else {
    s = v < 0;
    v = Math.abs(v);

    if (v >= Math.pow(2, 1 - bias)) {
      var ln = Math.min(Math.floor(Math.log(v) / Math.LN2), bias);
      e = ln + bias;
      f = v * Math.pow(2, fbits - ln) - Math.pow(2, fbits);
    } else {
      e = 0;
      f = v / Math.pow(2, 1 - bias - fbits);
    }
  }

  // Pack sign, exponent, fraction
  var i,
      bits = [];
  for (i = fbits; i; i -= 1) {
    bits.push(f % 2 ? 1 : 0);f = Math.floor(f / 2);
  }
  for (i = ebits; i; i -= 1) {
    bits.push(e % 2 ? 1 : 0);e = Math.floor(e / 2);
  }
  bits.push(s ? 1 : 0);
  bits.reverse();
  var str = bits.join('');

  // Bits to bytes
  var bytes = [];
  while (str.length) {
    bytes.push(parseInt(str.substring(0, 8), 2));
    str = str.substring(8);
  }
  return bytes;
};

Float.fromIEEE754 = function (bytes, ebits, fbits) {

  // Bytes to bits
  var bits = [];
  for (var i = bytes.length; i; i -= 1) {
    var byte = bytes[i - 1];
    for (var j = 8; j; j -= 1) {
      bits.push(byte % 2 ? 1 : 0);byte = byte >> 1;
    }
  }
  bits.reverse();
  var str = bits.join('');

  // Unpack sign, exponent, fraction
  var bias = (1 << ebits - 1) - 1;
  var s = parseInt(str.substring(0, 1), 2) ? -1 : 1;
  var e = parseInt(str.substring(1, 1 + ebits), 2);
  var f = parseInt(str.substring(1 + ebits), 2);

  // Produce number
  if (e === (1 << ebits) - 1) {
    return f !== 0 ? NaN : s * Infinity;
  } else if (e > 0) {
    return s * Math.pow(2, e - bias) * (1 + f / Math.pow(2, fbits));
  } else if (f !== 0) {
    return s * Math.pow(2, -(bias - 1)) * (f / Math.pow(2, fbits));
  } else {
    return s * 0;
  }
};

Float.fromIEEE754Double = function (b) {
  return Float.fromIEEE754(b, 11, 52);
};
Float.toIEEE754Double = function (v) {
  return Float.toIEEE754(v, 11, 52);
};
Float.fromIEEE754Single = function (b) {
  return Float.fromIEEE754(b, 8, 23);
};
Float.toIEEE754Single = function (v) {
  return Float.toIEEE754(v, 8, 23);
};

// Convert array of octets to string binary representation
// by bartaz

Float.toIEEE754DoubleString = function (v) {
  return exports.toIEEE754Double(v).map(function (n) {
    for (n = n.toString(2); n.length < 8; n = "0" + n);return n;
  }).join('').replace(/(.)(.{11})(.{52})/, "$1 $2 $3");
};