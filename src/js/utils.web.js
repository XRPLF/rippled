var exports = module.exports = require('./utils.js');

// We override this function for browsers, because they print objects nicer
// natively than JSON.stringify can.
exports.logObject = function (msg, obj) {
  if (/MSIE/.test(navigator.userAgent)) {
    console.log(msg, JSON.stringify(obj));
  } else {
    console.log(msg, "", obj);
  }
};
