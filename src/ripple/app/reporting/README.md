Reporting mode is a special operating mode of rippled, designed to handle RPCs
for validated data. A server running in reporting mode does not connect to the
p2p network, but rather extracts validated data from a node that is connected
to the p2p network. To run rippled in reporting mode, you must also run a
separate rippled node in p2p mode, to use as an ETL source. Multiple reporting
nodes can share access to the same network accessible databases (Postgres and
Cassandra); at any given time, only one reporting node will be performing ETL
and writing to the databases, while the others simply read from the databases.
A server running in reporting mode will forward any requests that require access
to the p2p network to a p2p node.

# Reporting ETL
A single reporting node has one or more ETL sources, specified in the config
file. A reporting node will subscribe to the "ledgers" stream of each of the ETL
sources. This stream sends a message whenever a new ledger is validated. Upon
receiving a message on the stream, reporting will then fetch the data associated
with the newly validated ledger from one of the ETL sources. The fetch is
performed via a gRPC request ("GetLedger"). This request returns the ledger
header, transactions+metadata blobs, and every ledger object
added/modified/deleted as part of this ledger. ETL then writes all of this data
to the databases, and moves on to the next ledger. ETL does not apply
transactions, but rather extracts the already computed results of those
transactions (all of the added/modified/deleted SHAMap leaf nodes of the state
tree). The new SHAMap inner nodes are computed by the ETL writer; this computation mainly
involves manipulating child pointers and recomputing hashes, logic which is
buried inside of SHAMap.

If the database is entirely empty, ETL must download an entire ledger in full
(as opposed to just the diff, as described above). This download is done via the
"GetLedgerData" gRPC request. "GetLedgerData" allows clients to page through an
entire ledger over several RPC calls. ETL will page through an entire ledger,
and write each object to the database.

If the database is not empty, the reporting node will first come up in a "soft"
read-only mode. In read-only mode, the server does not perform ETL and simply
publishes new ledgers as they are written to the database. 
If the database is not updated within a certain time period
(currently hard coded at 20 seconds), the reporting node will begin the ETL
process and start writing to the database. Postgres will report an error when
trying to write a record with a key that already exists. ETL uses this error to
determine that another process is writing to the database, and subsequently
falls back to a soft read-only mode. Reporting nodes can also operate in strict
read-only mode, in which case they will never write to the database.

# Database Nuances
The database schema for reporting mode does not allow any history gaps.
Attempting to write a ledger to a non-empty database where the previous ledger
does not exist will return an error.

The databases must be set up prior to running reporting mode. This requires
creating the Postgres database, and setting up the Cassandra keyspace. Reporting
mode will create the objects table in Cassandra if the table does not yet exist. 

Creating the Postgres database:
```
$ psql -h [host] -U [user]
postgres=# create database [database];
```
Creating the keyspace:
```
$ cqlsh [host] [port]
> CREATE KEYSPACE rippled WITH REPLICATION =
  {'class' : 'SimpleStrategy', 'replication_factor' : 3    };
```
A replication factor of 3 is recommended. However, when running locally, only a
replication factor of 1 is supported.

Online delete is not supported by reporting mode and must be done manually. The
easiest way to do this would be to setup a second Cassandra keyspace and
Postgres database, bring up a single reporting mode instance that uses those
databases, and start ETL at a ledger of your choosing (via --startReporting on
the command line). Once this node is caught up, the other databases can be
deleted.

To delete:
```
$ psql -h [host] -U [user] -d [database]
reporting=$ truncate table ledgers cascade;
```
```
$ cqlsh [host] [port]
> truncate table objects;
```
# Proxy
RPCs that require access to the p2p network and/or the open ledger are forwarded
from the reporting node to one of the ETL sources. The request is not processed
prior to forwarding, and the response is delivered as-is to the client.
Reporting will forward any requests that always require p2p/open ledger access
(fee and submit, for instance). In addition, any request that explicitly
requests data from the open or closed ledger (via setting
"ledger_index":"current" or "ledger_index":"closed"), will be forwarded to a
p2p node. 

For the stream "transactions_proposed" (AKA "rt_transactions"), reporting
subscribes to the "transactions_proposed" streams of each ETL source, and then
forwards those messages to any clients subscribed to the same stream on the
reporting node. A reporting node will subscribe to the stream on each ETL
source, but will only forward the messages from one of the streams at any given
time (to avoid sending the same message more than once to the same client).

# API changes
A reporting node defaults to only returning validated data. If a ledger is not
specified, the most recently validated ledger is used. This is in contrast to
the normal rippled behavior, where the open ledger is used by default.

Reporting will reject all subscribe requests for streams "server", "manifests",
"validations", "peer_status" and "consensus".

