'use strict';
var assert = require('assert');
var BigNumber = require('bignumber.js');

/*
The quality, as stored in the last 64 bits of a directory index, is stored as
the quotient of TakerPays/TakerGets. It uses drops (1e-6 XRP) for XRP values.
*/

function adjustQualityForXRP(quality, takerGetsCurrency, takerPaysCurrency) {
  var numeratorShift = (takerPaysCurrency === 'XRP' ? -6 : 0);
  var denominatorShift = (takerGetsCurrency === 'XRP' ? -6 : 0);
  var shift = numeratorShift - denominatorShift;
  return shift === 0 ? (new BigNumber(quality)).toString() :
    (new BigNumber(quality)).shift(shift).toString();
}

function parseQuality(qualityHex, takerGetsCurrency, takerPaysCurrency) {
  assert(qualityHex.length === 16);
  var mantissa = new BigNumber(qualityHex.substring(2), 16);
  var offset = parseInt(qualityHex.substring(0, 2), 16) - 100;
  var quality = mantissa.toString() + 'e' + offset.toString();
  return adjustQualityForXRP(quality, takerGetsCurrency, takerPaysCurrency);
}

module.exports = parseQuality;
