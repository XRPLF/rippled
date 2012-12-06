Function.prototype.method = function(name,func) {
  this.prototype[name] = func;

  return this;
};

var filterErr = function(code, done) {
  return function(e) {
      done(e.code !== code ? e : undefined);
    };
};

var throwErr = function(done) {
  return function(e) {
      if (e)
	throw e;
      
      done();
    };
};
 
var trace = function(comment, func) {
  return function() {
      console.log("%s: %s", trace, arguments.toString);
      func(arguments);
    };
};

var arraySet = function (count, value) {
  var a = new Array(count);
  var i;

  for (i = 0; i != count; i += 1)
    a[i] = value;

  return a;
};

var hexToString = function (h) {
  var	a = [];
  var	i = 0;

  if (h.length % 2) {
    a.push(String.fromCharCode(parseInt(h.substring(0, 1), 16)));
    i = 1;
  }

  for (; i != h.length; i += 2) {
    a.push(String.fromCharCode(parseInt(h.substring(i, i+2), 16)));
  }
  
  return a.join("");
};

var stringToHex = function (s) {
  return Array.prototype.map.call(s, function (c) {
      var b = c.charCodeAt(0);

      return b < 16 ? "0" + b.toString(16) : b.toString(16);
    }).join("");
};

var stringToArray = function (s) {
  var a = new Array(s.length);
  var i;

  for (i = 0; i != a.length; i += 1)
    a[i] = s.charCodeAt(i);

  return a;
};

var hexToArray = function (h) {
  return stringToArray(hexToString(h));
}

var chunkString = function (str, n, leftAlign) {
  var ret = [];
  var i=0, len=str.length;
  if (leftAlign) {
    i = str.length % n;
    if (i) ret.push(str.slice(0, i));
  }
  for(; i < len; i += n) {
    ret.push(str.slice(i, n+i));
  }
  return ret;
};

var logObject = function (msg, obj) {
  console.log(msg, JSON.stringify(obj, undefined, 2));
};

var assert = function (assertion, msg) {
  if (!assertion) {
    throw new Error("Assertion failed" + (msg ? ": "+msg : "."));
  }
};

exports.trace         = trace;
exports.arraySet      = arraySet;
exports.hexToString   = hexToString;
exports.hexToArray    = hexToArray;
exports.stringToArray = stringToArray;
exports.stringToHex   = stringToHex;
exports.chunkString   = chunkString;
exports.logObject     = logObject;
exports.assert        = assert;

// vim:sw=2:sts=2:ts=8:et
