Metrics
=======

This is an implementation of a beast::insight::Collector that exposes metrics
via a rest API at http://localhost:8181/.

REST API
########

- /metric/ - Returns a list of metric classes
  Example:
  GET /metric/ -> ["meter", "gauge", "event", "counter"]
- /metric/gauge/, /metric/meter/, etc - Returns a list of metrics in that class
  Example:
  GET /metric/counter/ -> ["ledger_fetches", "ledger.history.mismatch"]
- /metric/gauge/foo - Returns a map of metric measurements. This map is in the
  format of {<seconds-ago>: <value>}, where <seconds-ago> is the number of
  seconds ago since *now* a data point was recorded.
  Example:
  GET /metric/counter/ledger_fetches -> {"0": 30, "1": 29, "3": 4}
