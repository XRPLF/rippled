#!/usr/bin/node
//
// Returns hex of string.
//

var stringToHex = function (s) {
  return Array.prototype.map.call(s, function (c) {
      var b = c.charCodeAt(0);

      return b < 16 ? "0" + b.toString(16) : b.toString(16);
    }).join("");
};

if (process.argv.length != 3) {
  process.stderr.write("Usage: " + process.argv[1] + " string\n\nReturns hex of lowercasing string.\n");

} else {

  process.stdout.write(stringToHex(process.argv[2].toLowerCase()) + "\n");
}

// vim:sw=2:sts=2:ts=8:et
