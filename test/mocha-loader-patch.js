mocha = require("mocha")
// Stash a reference away to this
old_loader = mocha.prototype.loadFiles

// Optionally use a more useful (but noisy) logger
if (process.env.USE_RCONSOLE) {
  require('rconsole');
};

if (!old_loader.monkey_patched) {
  // Gee thanks Mocha ...
  mocha.prototype.loadFiles = function() {
    try {
      old_loader.apply(this, arguments);
    } catch (e) {
      // Normally mocha just silently bails
      console.error(e.stack);
      // We throw, so mocha doesn't continue trying to run the test suite
      throw e;
    }
  }
  mocha.prototype.loadFiles.monkey_patched = true;
};

