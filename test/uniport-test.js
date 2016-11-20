/* -------------------------------- REQUIRES -------------------------------- */

var async       = require("async");
var assert      = require('assert');
var assert      = require('assert');
var UInt160     = require('ripple-lib').UInt160;
var Remote      = require('ripple-lib').Remote;
var Server      = require('ripple-lib').Server;
var Request     = require('ripple-lib').Request;
var testutils   = require("./testutils");
var config      = testutils.init_config();
var http        = require('http');
var request     = require('request');

/* --------------------------------- CONFIG --------------------------------- */

// So we can connect to the ports with self signed certificates.
process.env.NODE_TLS_REJECT_UNAUTHORIZED = "0";

var uniport_test_config = {
  // We have one main WebSocket connection that we inherit from
  // `testutils.build_setup`. We use that, configured on a non admin, non ssl
  // port, to determine readiness for tests on the other ports. This Remote's
  // trace is configured in the normal way, however we can set trace for each of
  // the test connections on the various ports.
  remote_trace : false,

  // We can be a bit more exhaustive, but it will cost us 2 seconds, unless
  // we use `skip_tests_matching` to `test.skip` them.
  define_redundant_tests : false,

  skip_tests_matching : [
    // /redundant/

    // /can not issue/,
    // /http/
    // /ws/
  ],

  skip_tests_not_matching : [
    // /wrong password/,
    // /ws/
  ],

  skip_ports_not_matching : [
    // /admin/,
    // /password/,
    // /http/
  ],

  skip_ports_matching : [
    // /admin/,
    // /password/,
    // /http/
  ]
};

/* --------------------------------- HELPERS -------------------------------- */

function for_each_item (o, f) {
  for (var k in o) {
    if (o.hasOwnProperty(k)) {
      f(k, o[k], o);
    }
  }
};

function pretty_json (o) {
  return JSON.stringify(o, undefined, 2);
};

function client_protocols (conf) {
  return conf.protocol.split(',').filter(function (p) {return p !== 'peer';});
};

function same_protocol_opposite_security (protocol) {
  switch(protocol)
  {
    case 'ws':
      return 'wss';
    case 'http':
      return 'https';
    case 'wss':
      return 'ws';
    case 'https':
      return 'http';
    default:
      throw new Error('unknown protocol '+ protocol);
  }
};

function one_invocation_function (f) {
  var already_done = false;
  return function done_once() {
    try {
      if (!already_done) {
        f();
      }
    } finally {
      already_done = true;
    }
  };
};

function mark_redundant_tests (normalizer) {
  var tested = [];

  return function (test_name, func) {
    var normed = normalizer ? normalizer(test_name) : test_name;
    var redundant = ~tested.indexOf(normed);

    if (redundant)
      test_name += ' (redundant)';
    else
      tested.push(normed);

    if (!redundant || uniport_test_config.define_redundant_tests) {
      define_test(test_name, func);
    }
  };
};

function add_credentials_to_request (message, credentials, wrong_pass,
                                                                wrong_user)
{
  message.admin_user = credentials.admin_user;
  message.admin_password = credentials.admin_password;

  if (wrong_pass) {
    if (wrong_pass === 'send_object_instead_of_string') {
      message.admin_password = {admin_password: message.admin_password};
    } else {
       message.admin_password += '_';
    }
  }

  if (wrong_user) {
    message.admin_user += '_';
  }
};

var define_test = testutils.definer_matching(
    {
      skip_if_not_match:uniport_test_config.skip_tests_not_matching,
      skip_if_match:uniport_test_config.skip_tests_matching
    },
    global.test );

var define_suite = testutils.definer_matching(
    {
      skip_if_not_match:uniport_test_config.skip_ports_not_matching,
      skip_if_match:uniport_test_config.skip_ports_matching
    },
    global.suite );

/* ---------------------------------- TESTS --------------------------------- */


function test_websocket_admin_command (test_declaration,
                                             protocol,
                                             port_conf, done)
{
  var expect_success = test_declaration.expect_success;
  var expect_failure = !expect_success;
  var require_pass = Boolean(port_conf.admin_password);
  var send_credentials = test_declaration.send_credentials;
  var wrong_pass = test_declaration.wrong_pass;
  var wrong_user = test_declaration.wrong_user;

  var config = {
    servers : [protocol + '://127.0.0.1:' + port_conf.port],
    'trace' : uniport_test_config.remote_trace
  };

  var remote = Remote.from_config(config);

  if (require_pass) {
    remote.on('prepare_subscribe', function (request){
      request.once('error', function (e, m){
          assert.notEqual(e.remote.error,
                          'forbidden',
                          'Need credentials for non admin request (subscribe)');
      });
    });
  }
  remote.connect( function () {
    var before_accept = remote._ledger_current_index;
    var request = new Request(remote, 'ledger_accept');

    if (send_credentials) {
      add_credentials_to_request( request.message, port_conf,
                                  wrong_pass, wrong_user);
    }

    request.callback(function (error, response){
      // Disconnect
      remote.disconnect();

      function create_error (message) {
        var struct = {port_conf: port_conf,
                      request: request.message,
                      error: error,
                      response: response,
                      test_failure: message};
        return pretty_json(struct);
      };

      if (error) {
        assert(expect_failure,
              create_error('unexpect failure to issue admin command'));

        if (expect_failure) {
          if (require_pass && (!send_credentials || wrong_pass || wrong_user)) {
            assert.equal(error.remote.error,
                         'forbidden',
                         create_error('should be forbidden'));

          } else if (!require_pass) {
            assert.equal(error.remote.error,
                        'forbidden',
                         create_error('should be forbidden'));
          }
        }
      }

      if (response) {
        if (expect_success) {
          assert.equal((before_accept + 1), response.ledger_current_index,
                       create_error('admin command should work but did not'));
        } else {
          assert.equal(before_accept, response.ledger_current_index,
                       create_error('admin command worked but should not have'));
        }
      }
      done();
    });
  });
};

function test_http_admin_command (test_declaration, protocol, conf, done)
{
  var expect_success = test_declaration.expect_success;
  var expect_failure = !expect_success;
  var require_pass = Boolean(conf.admin_password);
  var send_credentials = test_declaration.send_credentials;
  var wrong_pass = test_declaration.wrong_pass;
  var wrong_user = test_declaration.wrong_user;

  var url = protocol+'://localhost:'+conf.port + '/';

  var post_options = {
    url: url,
    json: true,
    body: {
      method: 'ledger_accept',
    }
  };

  if (send_credentials) {
    var credentials = {};
    post_options.body.params = [credentials];
    add_credentials_to_request(credentials, conf, wrong_pass, wrong_user);
  }

  request.post(post_options, function (err, response, body) {
    function create_error (message) {
      var struct = {port_conf: conf,
                    request: post_options,
                    error: err,
                    response: body,
                    statusCode: response.statusCode,
                    test_failure: message};
      return pretty_json(struct);
    };

    if (err) {
      assert(!err, String(err));
    }

    if (expect_failure)
    {
      if (!body) {
        assert.equal(response.statusCode, 403);
        assert (false, create_error("we expect some kind of response body"));
      }
      else if (typeof body == 'string') {
        assert.equal(response.statusCode, 403);
        assert.equal(body.trim(), 'Forbidden');
      }
      else {
        assert(body.result.status != 'success',
              create_error("succeded when shouldn't have"));
      }
      done();
    }
    else {
      var msg = "expected 200 got " + response.statusCode+'\n'+pretty_json(body);
      assert.equal(response.statusCode, 200, msg);

      if (body && body.result) {
        assert.equal(body.result.status, 'success', pretty_json(body));
      }
      done();
    }
  });
};

function test_admin_command (test_declaration, protocol,
                                   port_conf, done) {

  var type = protocol.slice(0, 2);
  if (type == 'ws') {
    test_websocket_admin_command(test_declaration, protocol, port_conf, done);
  }
  else if (type == 'ht') {
    test_http_admin_command(test_declaration, protocol, port_conf, done);
  }
  else {
    throw new Error('unknown protocol: ' + protocol);
  }
};

function test_cant_connect (port_conf, protocol, done) {
  var type = protocol.slice(0, 2);

  if (type == 'ws') {
    var done_once = one_invocation_function (done);

    var WebSocket = Server.websocketConstructor();
    var ws_url = protocol+'://localhost:'+port_conf.port + '/';
    var ws = new WebSocket(ws_url);

    ws.onopen = function () {
      assert(false);
    };

    if (protocol == 'wss') {
      setTimeout(function () {
        //console.log("----> Timeout, readyState: " + ws.readyState)
        //assert.equal(ws.readyState, 0);
        assert(true);
        done_once();
      }, 200);
    }

    ws.onerror = ws.onclose = function (m) {
      //console.log("----> Close, readyState: " + ws.readyState)
      assert(true);
      done_once();
    };
  } else if (type == 'ht') {
    var url = protocol+'://localhost:'+port_conf.port + '/';

    var post_options = {
      url: url,
      json: true,
      body: {
        method: 'server_info',
      }
    };

    request.post(post_options, function (err, response, body) {
      assert(!body);
      assert(!response);
      assert(err);
      done();
    });

  } else {
    throw new Error('unknown protocol: ' + protocol);
  }
};

suite("Uniport tests", function () {
  var $ = { };

  suiteSetup(function (done) {
    testutils.build_setup({}, 'uniport_tests').call($, function () {
      done();
    });
  });

  suiteTeardown(function (done) {
    testutils.build_teardown('uniport_tests').call($, done);
  });

  suite('connection', function () {
    var define_test = mark_redundant_tests();

    for_each_item(config.uniport_test_ports, function (name, conf) {
      var protocols = client_protocols(conf);

      define_suite(name, function () {
        ['http', 'https', 'ws', 'wss'].forEach(function (p) {
          var op = same_protocol_opposite_security(p);
          if (!(~protocols.indexOf(p)) &&
               ~(protocols.indexOf(op))) {

            var test_name = "can't connect using " + p +
                            " with only " + op + " set";

            define_test( test_name,
              function (done){
                test_cant_connect(conf, p, done);
            });
          }
        });
      });
    });
  });

  suite('admin commands', function () {
    var define_test = mark_redundant_tests(function (test_name) {
      // The password checking should be the same regardless of protocol,
      // so we normalize the test name (used to determine test redundancy).
      return test_name.replace('wss', 'ws')
                      .replace('https', 'http');
    });

    for_each_item(config.uniport_test_ports, function (name, conf) {
      define_suite(name, function () {
        var protocols = client_protocols(conf);

        var allow_admin = conf.admin !== '';
        var require_pass = Boolean(conf.admin_password);

        function test_for (protocol, params) {
          return function (done) {
            test_admin_command(params, protocol, conf, done);
          };
        };

        if (allow_admin && require_pass) {
          protocols.forEach(function (protocol) {
            var can_not_issue_admin_commands =
                  'can not issue admin commands on '+ protocol + ' ';

            define_test(
              ('can issue admin commands on '+protocol+
                ' with correct credentials'),
               test_for(protocol,
                        {expect_success: true, send_credentials: true})
            );
            define_test(
              can_not_issue_admin_commands + 'with wrong password',
              test_for(protocol, {expect_success: false,
                                  send_credentials: true,
                                  wrong_pass: true})
            );
            define_test(
              can_not_issue_admin_commands + 'with garbage password',
              test_for(protocol, {expect_success: false,
                                  send_credentials: true,
                                  wrong_pass: 'send_object_instead_of_string'})
            );
            define_test(
              can_not_issue_admin_commands + ' with wrong user',
              test_for(protocol, {expect_success: false,
                                  send_credentials: true,
                                  wrong_user: true})
            );
            define_test(
              can_not_issue_admin_commands + 'without credentials',
              test_for(protocol, {expect_success: false,
                                  send_credentials: false})
            );
          });
        }
        else if (allow_admin)
        {
          protocols.forEach(function (protocol) {
            define_test(
              'can issue admin commands on ' + protocol,
              test_for(protocol, {expect_success: true,
                                  send_credentials: false})
            );
          });
        }
        else if (!allow_admin)
        {
          protocols.forEach(function (protocol) {
            define_test(
              'can not issue admin commands on ' + protocol,
              test_for(protocol, {expect_success: false,
                                  send_credentials: false})
            );
          });
        }
      });
    });
  });
});
