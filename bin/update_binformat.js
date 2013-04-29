/**
 * bin/update_bintypes.js
 *
 * This unholy abomination of a script generates the JavaScript file
 * src/js/bintypes.js from various parts of the C++ source code.
 *
 * This should *NOT* be part of any automatic build process unless the C++
 * source data are brought into a more easily parseable format. Until then,
 * simply run this script manually and fix as needed.
 */

// XXX: Process LedgerFormats.(h|cpp) as well.

var filenameProto = __dirname + '/../src/cpp/ripple/SerializeProto.h',
    filenameTxFormatsH = __dirname + '/../src/cpp/ripple/TransactionFormats.h',
    filenameTxFormats = __dirname + '/../src/cpp/ripple/TransactionFormats.cpp';

var fs = require('fs');

var output = [];

// Stage 1: Get the field types and codes from SerializeProto.h
var types = {},
    fields = {};
String(fs.readFileSync(filenameProto)).split('\n').forEach(function (line) {
  line = line.replace(/^\s+|\s+$/g, '').replace(/\s+/g, '');
  if (!line.length || line.slice(0, 2) === '//' || line.slice(-1) !== ')') return;

  var tmp = line.slice(0, -1).split('('),
      type = tmp[0],
      opts = tmp[1].split(',');

  if (type === 'TYPE') types[opts[1]] = [opts[0], +opts[2]];
  else if (type === 'FIELD') fields[opts[0]] = [types[opts[1]][0], +opts[2]];
});

output.push('var ST = require("./serializedtypes");');
output.push('');
output.push('var REQUIRED = exports.REQUIRED = 0,');
output.push('    OPTIONAL = exports.OPTIONAL = 1,');
output.push('    DEFAULT  = exports.DEFAULT  = 2;');
output.push('');

function pad(s, n) { while (s.length < n) s += ' '; return s; }
function padl(s, n) { while (s.length < n) s = ' '+s; return s; }

Object.keys(types).forEach(function (type) {
  output.push(pad('ST.'+types[type][0]+'.id', 25) + ' = '+types[type][1]+';');
});
output.push('');

// Stage 2: Get the transaction type IDs from TransactionFormats.h
var ttConsts = {};
String(fs.readFileSync(filenameTxFormatsH)).split('\n').forEach(function (line) {
  var regex = /tt([A-Z_]+)\s+=\s+([0-9-]+)/;
  var match = line.match(regex);
  if (match) ttConsts[match[1]] = +match[2];
});

// Stage 3: Get the transaction formats from TransactionFormats.cpp
var base = [],
    sections = [],
    current = base;
String(fs.readFileSync(filenameTxFormats)).split('\n').forEach(function (line) {
  line = line.replace(/^\s+|\s+$/g, '').replace(/\s+/g, '');

  var d_regex = /DECLARE_TF\(([A-Za-z]+),tt([A-Z_]+)/;
  var d_match = line.match(d_regex);

  var s_regex = /SOElement\(sf([a-z]+),SOE_(REQUIRED|OPTIONAL|DEFAULT)/i;
  var s_match = line.match(s_regex);

  if (d_match) sections.push(current = [d_match[1], ttConsts[d_match[2]]]);
  else if (s_match) current.push([s_match[1], s_match[2]]);
});

function removeFinalComma(arr) {
  arr[arr.length-1] = arr[arr.length-1].slice(0, -1);
}

output.push('var base = [');
base.forEach(function (field) {
  var spec = fields[field[0]];
  output.push('  [ '+
              pad("'"+field[0]+"'", 21)+', '+
              pad(field[1], 8)+', '+
              padl(""+spec[1], 2)+', '+
              'ST.'+pad(spec[0], 3)+
              ' ],');
});
removeFinalComma(output);
output.push('];');
output.push('');


output.push('exports.tx = {');
sections.forEach(function (section) {
  var name = section.shift(),
      ttid = section.shift();

  output.push('  '+name+': ['+ttid+'].concat(base, [');
  section.forEach(function (field) {
    var spec = fields[field[0]];
    output.push('    [ '+
                pad("'"+field[0]+"'", 21)+', '+
                pad(field[1], 8)+', '+
                padl(""+spec[1], 2)+', '+
                'ST.'+pad(spec[0], 3)+
                ' ],');
  });
  removeFinalComma(output);
  output.push('  ]),');
});
removeFinalComma(output);
output.push('};');
output.push('');

console.log(output.join('\n'));

