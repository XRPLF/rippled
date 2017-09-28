'use strict';

var assert = require('assert');

/**
 * Logging functionality for ripple-lib and any applications built on it.
 *
 * @param {String} namespace logging prefix
 * @return {Void} this function does not return...
 */
function Log(namespace) {
  if (!namespace) {
    this._namespace = [];
  } else if (Array.isArray(namespace)) {
    this._namespace = namespace;
  } else {
    this._namespace = [String(namespace)];
  }

  this._prefix = this._namespace.concat(['']).join(': ');
}

/**
 * Create a sub-logger.
 *
 * You can have a hierarchy of loggers.
 *
 * @example
 *
 *   var log = require('ripple').log.sub('server');
 *
 *   log.info('connection successful');
 *   // prints: 'server: connection successful'
 *
 * @param {String} namespace logging prefix
 * @return {Log} sub logger
 */
Log.prototype.sub = function (namespace) {
  var subNamespace = this._namespace.slice();

  if (namespace && typeof namespace === 'string') {
    subNamespace.push(namespace);
  }

  var subLogger = new Log(subNamespace);
  subLogger._setParent(this);
  return subLogger;
};

Log.prototype._setParent = function (parentLogger) {
  this._parent = parentLogger;
};

Log.makeLevel = function (level) {
  return function () {
    var args = Array.prototype.slice.apply(arguments);
    args[0] = this._prefix + args[0];
    Log.engine.logObject.apply(Log, [level].concat(args[0], [args.slice(2)]));
  };
};

Log.prototype.debug = Log.makeLevel(1);
Log.prototype.info = Log.makeLevel(2);
Log.prototype.warn = Log.makeLevel(3);
Log.prototype.error = Log.makeLevel(4);

/**
 * @param {String} message
 * @param {Array} details
 * @return {Array} prepared log info
 */

function getLogInfo(message, args) {
  var stack = new Error().stack;

  return [
  // Timestamp
  '[' + new Date().toISOString() + ']', message, '--',
  // Location
  typeof stack === 'string' ? stack.split('\n')[4].replace(/^\s+/, '') : '', '\n'].concat(args);
}

/**
 * @param {Number} log level
 * @param {Array} log info
 */

function logMessage(logLevel, args) {
  switch (logLevel) {
    case 1:
    case 2:
      console.log.apply(console, args);
      break;
    case 3:
      console.warn.apply(console, args);
      break;
    case 4:
      console.error.apply(console, args);
      break;
  }
}

var engines = {};

/**
 * Basic logging connector.
 *
 * This engine has no formatting and works with the most basic of 'console.log'
 * implementations. This is the logging engine used in Node.js.
 */
engines.basic = {
  logObject: function logObject(level, message, args_) {
    var args = args_.map(function (arg) {
      return JSON.stringify(arg, null, 2);
    });

    logMessage(level, getLogInfo(message, args));
  }
};

/**
 * Log engine for browser consoles.
 *
 * Browsers tend to have better consoles that support nicely formatted
 * JavaScript objects. This connector passes objects through to the logging
 * function without any stringification.
 */
engines.interactive = {
  logObject: function logObject(level, message, args_) {
    var args = args_.map(function (arg) {
      return (/MSIE/.test(navigator.userAgent) ? JSON.stringify(arg, null, 2) : arg
      );
    });

    logMessage(level, getLogInfo(message, args));
  }
};

/**
 * Null logging connector.
 *
 * This engine simply swallows all messages. Used when console.log is not
 * available.
 */
engines.none = {
  logObject: function logObject() {}
};

Log.getEngine = Log.prototype.getEngine = function () {
  return Log.engine;
};

Log.setEngine = Log.prototype.setEngine = function (engine) {
  assert.strictEqual(typeof engine, 'object');
  assert.strictEqual(typeof engine.logObject, 'function');
  Log.engine = engine;
};

if (typeof window !== 'undefined' && typeof console !== 'undefined') {
  Log.setEngine(engines.interactive);
} else if (typeof console !== 'undefined' && console.log) {
  Log.setEngine(engines.basic);
} else {
  Log.setEngine(engines.none);
}

/**
 * Provide a root logger as our main export.
 *
 * This means you can use the logger easily on the fly:
 *     ripple.log.debug('My object is', myObj);
 */
module.exports = new Log();

/**
 * This is the logger for ripple-lib internally.
 */
module.exports.internal = module.exports.sub();

/**
 * Expose the class as well.
 */
module.exports.Log = Log;

/**
 * Expose log engines
 */
module.exports.engines = engines;