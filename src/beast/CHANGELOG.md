1.0.0-bNN

* Add WebSocket permessage-deflate extension support
* Fix message constructor and special members

--------------------------------------------------------------------------------

1.0.0-b22

* Fix broken Intellisense
* Implement the Asio deallocation-before-invocation guarantee
* Add handler helpers
* Avoid copies in handler_alloc
* Update README.md example programs
* Fix websocket stream read documentation
* Disable Boost.Coroutine deprecation warning
* Update documentation examples

--------------------------------------------------------------------------------

1.0.0-b21

* Remove extraneous includes

--------------------------------------------------------------------------------

1.0.0-b20

ZLib

* Add ZLib module

API Changes:

* Rename HTTP identifiers

--------------------------------------------------------------------------------

1.0.0-b19

* Boost library min/max guidance
* Improvements to code coverage
* Use boost::lexical_cast instead of std::to_string
* Fix prepare_buffers value_type
* Fix consuming_buffers value_type
* Better buffer_cat

HTTP

* Make chunk_encode public
* Add write, async_write, operator<< for message_headers
* Add read, async_read for message_headers
* Fix with_body example

WebSocket

* Optimize utf8 validation
* Optimize mask operations

API Changes:

* Refactor message and message_headers declarations
* prepared_buffers is private
* consume_buffers is removed

--------------------------------------------------------------------------------

1.0.0-b18

* Increase optimization settings for MSVC builds

HTTP

* Check invariants in parse_op:
* Clean up message docs

WebSocket

* Write buffer option does not change capacity
* Close connection during async_read on close frame
* Add pong, async pong to stream

Core

* Meet DynamicBuffer requirements for static_streambuf
* Fix write_frame masking and auto-fragment handling

Extras

* unit_test::suite fixes:
  - New overload of fail() specifies file and line
  - BEAST_EXPECTS only evaluates the reason string on a failure
* Add zlib module

--------------------------------------------------------------------------------

1.0.0-b17

* Change implicit to default value in example
* Tidy up some declarations
* Fix basic_streambuf::capacity
* Add basic_streambuf::alloc_size
* Parser callbacks may not throw
* Fix Reader concept doc typo
* Add is_Reader trait
* Tidy up basic_headers for documentation
* Tidy up documentation
* Add basic_parser_v1::reset
* Fix handling of body_what::pause in basic_parser_v1
* Add headers_parser
* Engaged invokable is destructible
* Improve websocket example in README.md
* Refactor read_size_helper

API Changes:

* Added init() to Reader requirements
* Reader must be nothrow constructible
* Reader is now constructed right before reading the body
  - The message passed on construction is filled in
* Rework HTTP concepts:
  - Writer uses write instead of operator()
  - Refactor traits to use void_t
  - Remove is_ReadableBody, is_WritableBody
  - Add has_reader, has_writer, is_Reader, is_Writer
  - More friendly compile errors on failed concept checks
* basic_parser_v1 requires all callbacks present
* on_headers parser callback now returns void
* on_body_what is a new required parser callback returning body_what

--------------------------------------------------------------------------------

1.0.0-b16

* Make value optional in param-list
* Frame processing routines are member functions
* Fix on_headers called twice from basic_parser_v1
* Constrain parser_v1 constructor
* Improve first line serialization
* Add pause option to on_headers interface
* Refactor base_parser_v1 callback traits:
* Refine Parser concept
* Relax ForwardIterator requirements in FieldSequence
* Fix websocket failure testing
* Refine Writer concept and fix exemplar in documentation

API Changes:

* Rename mask_buffer_size to write_buffer_size
* Make auto_fragment a boolean option

The message class hierarchy is refactored (breaking change):

* One message class now models both HTTP/1 and HTTP/2 messages
* message_v1, request_v1, response_v1 removed
* New classes basic_request and basic_response model
  messages without the body.

    Error resolution: Callers should use message, request,
    and response instead of message_v1, request_v1, and
    response_v1 respectively.

--------------------------------------------------------------------------------

1.0.0-b15

* rfc7230 section 3.3.2 compliance
* Add HTTPS example
* Add Secure WebSocket example
* Fix message_v1 constructor
* Tidy up DynamicBuffer requirements
* Tidy up error types and headers
* Fix handling empty HTTP headers in parser_v1

--------------------------------------------------------------------------------

1.0.0-b14

* Add missing rebind to handler_alloc
* Fix error handling in http server examples
* Fix CMake scripts for MinGW
* Use BOOST_ASSERT
* Better WebSocket decorator
* Update and tidy documentation

--------------------------------------------------------------------------------

1.0.0-b13

* dstream improvements
* Remove bin and bin64 directories
* Tidy up .vcxproj file groupings

--------------------------------------------------------------------------------

1.0.0-b12

* Use -p to print suites from unit test main.
* BEAST_EXPECTS to add a reason string to test failures
* Fix unit test runner to output all case names
* Update README for build requirements
* Rename to CHANGELOG.md

--------------------------------------------------------------------------------

1.0.0-b11

* Set URI in generated WebSocket Upgrade requests
* Rename echo server class and file names
* Rename to DynamicBuffer in some code and documentation
* Fix integer warnings in Windows builds
* Add 32 and 64 bit Windows build support
* Update README for build instructions and more
* Add repository and documention banners

--------------------------------------------------------------------------------

1.0.0-b10

* Fix compilation warnings
* Add websocketpp comparison to HTML documentation

--------------------------------------------------------------------------------

1.0.0-b9

* Fix CMakeLists.txt

--------------------------------------------------------------------------------

1.0.0-b8

* Fix include in example code
* Fix basic_headers rfc2616 Section 4.2 compliance

--------------------------------------------------------------------------------

1.0.0-b7

* Fix prepare by calling init. prepare() can throw depending on the
  implementation of Writer. Publicly provided beast::http writers never throw.
* Fixes to example HTTP server
* Fully qualify ambiguous calls to read and parse
* Remove deprecated http::stream wrapper
* Example HTTP server now calculates the MIME-type
* Fixes and documentation for teardown and use with SSL:
* Add example code to rfc7230 javadocs
* Remove extraneous header file <beast/http/status.hpp>
* Add skip_body parser option

--------------------------------------------------------------------------------

1.0.0-b6

* Use SFINAE on return values
* Use beast::error_code instead of nested types
* Tidy up use of GENERATING_DOCS
* Remove obsolete RFC2616 functions
* Add message swap members and free functions
* Add HTTP field value parser containers: ext_list, param_list, token_list
* Fixes for some corner cases in basic_parser_v1
* Configurable limits on headers and body sizes in basic_parser_v1

API Changes:

* ci_equal is moved to beast::http namespace, in rfc7230.hpp

* "DynamicBuffer","dynabuf" renamed from "Streambuf", "streambuf". See:
  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4478.html#requirements.dynamic_buffers

* basic_parser_v1 adheres to rfc7230 as strictly as possible

--------------------------------------------------------------------------------
