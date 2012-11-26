exports = module.exports = require('./utils.js');

// We override this function for browsers, because they print objects nicer
// natively than JSON.stringify can.
exports.logObject = function (msg, obj) {
  console.log(msg, "", obj);
};
