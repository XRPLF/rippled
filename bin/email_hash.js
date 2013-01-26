#!/usr/bin/node
//
// Returns a Gravatar style hash as per: http://en.gravatar.com/site/implement/hash/
//

if (3 != process.argv.length) {
  process.stderr.write("Usage: " + process.argv[1] + " email_address\n\nReturns gravatar style hash.\n");
  process.exit(1);

} else {
  var md5 = require('crypto').createHash('md5');

  md5.update(process.argv[2].trim().toLowerCase());

  process.stdout.write(md5.digest('hex') + "\n");
}

// vim:sw=2:sts=2:ts=8:et
