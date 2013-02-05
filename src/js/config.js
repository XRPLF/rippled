// This object serves as a singleton to store config options

var extend = require("extend");

var config = module.exports = {
  load: function (newOpts) {
    extend(config, newOpts);
    return config;
  }
};
