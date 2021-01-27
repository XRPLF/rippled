# Shard Size Tuning

The purpose of this document is to compare the sizes of shards containing
varying amounts of ledgers.

## Methodology

One can see visually from a block explorer that a typical mainnet ledger
consists of about 30 offer transactions issued by about 8 different accounts,
and several transactions of other types. To simulate this situation and
similar situations we have constructed deterministic shards of differenet
sizes, with varying amounts of offers per ledger and varying amounts of
accounts issuing these offers.

In the following results table, the number of ledgers per shard ranges from 256
to 16K with the size doubling the size at each step. We considered the
following numbers of offers per ledger: 0, 1, 5, 10 and 30. Also we considered
case of 1 and 8 accounts issuing offers. For each constructed deterministic
shard we counted its size. Finally we compared doubled size of the shard with
N ledgers and the size of a shard with 2*N ledgers where othere parameters such
as number of offers and accounts are the same. This comparison is sufficient to
determine which number of ledgers per shard leads to less storage size on the
disk.

Note that we minimize total storage size on the disk, but not the size of each
shard because data below shows that the size of a typical shard is not larger
than 10G, but sizes of modern disks, even SSDs, start from 250G. So there is
no problem to fit a single shard to a disk, even small.


## Raw results table

All sizes of constructed shards are shown in the following table.
Rows corresponds to shard sizes (S) counted in ledgers, columns corresponds
to numbers of offers (O) per ledger. In each cell there are two numbers:
first number corresponds to the case of 1 account issuing offers, the second
number corresponds to 8 accounts. Each number is a size of the shard with
given parameters measured in megabytes.

|S\O|0|1|5|10|30|
|---|---|---|---|---|---|
|256|2.2/2.2|3.4/3.3|5.3/7.3|7.7/10.9|17.1/21.9|
|512|4.4/4.5|7.0/7.0|11.2/15.6|16.4/23.7|36.9/47.9|
|1K|8.9/9.0|14.7/14.6|23.7/33.6|35.0/51.0|78.2/ 102.9|
|2K|17.8/18.0|30.5/30.7|50.4/72.2|74.3/ 111.9|166.2/ 221.0|
|4K|35.5/35.9|63.6/64.2|106.2/ 154.8|156.1/ 238.7|354.7/ 476.0|
|8K|71.1/71.9|133.4/ 134.5|222.2/ 328.1|329.1/ 511.2|754.6/ 1021.0|
|16K|142.3/ 143.9|279/9 280.8|465.7/ 698.1|696.4/ 1094.2|1590.5/ 2166.6|

## Preliminary conclusion

If one compares a doubled size of shard with N ledgers and a size of shard
with 2*N ledgers anywhere in the above table than the conlusion will be that
the second number is greater. For example, the following table shows the
percentage by which the second number is greater for the most interesting case
of 30 offers per ledger. The first row corresponds to the case of 1 account
issuing offers, and the second row corresponds to the case of 8 issuing
accounts.

|A\N|256|512|1K|2K|4K|8K|
|---|---|---|---|---|---|---|
|1|8%|6%|6%|6%|7%|6%|5%|
|8|9%|7%|7%|8%|6%|7%|6%|

The common conclusion in this model is that if one doubled the number of 
the ledgers in a shard then the total disk space utilized will raise by 5-9%.

## Adding accounts into consideration

Previous model does not take into account that there are large number of
XRP accounts in the mainnet, and each shard should contain information
about each of these accounts. As of January 2020, there were about 1.9 million
XRP accounts, and stored information about each of them is not less than 133
bytes. The constant 133 was obtained from debug print of rippled program when
it saves account object to the database. So the actual size of each shard from
raw table should be increased by at least 1.9M * 133 = 252.7M. Thus we obtained
the following table of shard sizes for the most interesting case (30 offers
per ledger and 8 issuing accounts) where S is shard size in ledgers and M is
shard size in megabytes

|S|256|512|1K|2K|4K|8K|16K|
|---|---|---|---|---|---|---|---|
|M|274.6|300.6|355.6|473.7|728.7|1273.7|2419.3|

Now we can see from the last table that even considering minimum assumption
about number of accounts and corresponding additional size of a shard,
doubled size of shard with N ledgers is larger than size of a shard with
2*N ledgers. If number of accounts increase then this inequality will be
even stronger.

## Using mainnet data

Next idea to improve model is to count real shard sizes from mainnet.
We used real 16k-ledgers shards with indexes from 2600 to 3600 with step 100,
and corresponding real 8k-ledgers shards. Each 16k-ledgers shard consists
of two 8k-ledgers shards which are called "corresponding". For example,
16k-ledgers shard with index 2600 consists of two 8k-ledgers shards with
indexes 5200 and 5201.

In the following table we compare size of a 16k-ledgers shard with sum of sizes
of two corresponding 8k-ledgers shards. There we only count size of nudb.dat
file, sizes are in GB. Ratio is the size of two 8k-ledgers shards divided
to the size of 16k-ledgers shard.

|Index|16k-ledgers|8k-ledgers sum|Ratio|
|---|---|---|---|
|2600|2.39|1.49 + 1.63 = 3.12|1.31|
|2700|2.95|1.77 + 1.94 = 3.71|1.26|
|2800|2.53|1.54 + 1.75 = 3.29|1.30|
|2900|3.83|2.26 + 2.35 = 4.61|1.20|
|3000|4.49|2.70 + 2.59 = 5.29|1.18|
|3100|3.79|2.35 + 2.25 = 4.60|1.21|
|3200|4.15|2.54 + 2.43 = 4.97|1.20|
|3300|5.19|3.23 + 2.80 = 6.03|1.16|
|3400|4.18|2.53 + 2.51 = 5.04|1.21|
|3500|5.06|2.90 + 3.04 = 5.94|1.17|
|3600|4.18|2.56 + 2.51 = 5.07|1.21|
|Average|3.89|2.35 + 2.35 = 4.70|1.21|

Note that shard on the disk consists of 4 files each of which can be large too.
These files are nudb.dat, nudb.key, ledger.db, transaction.db. Next table is
similar to previous with the following exception: each number is total size
of these 2 files: nudb.dat and nudb.key. We decided not to count sizes of
ledger.db and transaction.db since these sizes are not permanent instead of
sizes of nudb.* which are permanent for deterministic shards.

|Index|16k-ledgers|8k-ledgers sum|Ratio|
|---|---|---|---|
|2600|2.76|1.73 + 1.89 = 3.62|1.31|
|2700|3.40|2.05 + 2.25 = 4.30|1.26|
|2800|2.91|1.79 + 2.02 = 3.81|1.31|
|2900|4.40|2.62 + 2.71 = 5.33|1.21|
|3000|5.09|3.09 + 2.96 = 6.05|1.19|
|3100|4.29|2.69 + 2.57 = 5.26|1.23|
|3200|4.69|2.90 + 2.78 = 5.68|1.21|
|3300|5.92|3.72 + 3.21 = 6.93|1.17|
|3400|4.77|2.91 + 2.89 = 5.80|1.22|
|3500|5.73|3.31 + 3.47 = 6.78|1.18|
|3600|4.77|2.95 + 2.90 = 5.85|1.23|
|Average|4.43|2.70 + 2.70 = 5.40|1.22|

We can see that in all tables ratio is greater then 1, so using shards with
16 ledgers is preferred.

## Compare 16K shards and 32K shards

To claim that shards with 16K ledgers are the best choice, we also assembled
shards with 32k ledgers per shard with indexes from 1300 to 1800 with step 50
and corresponding shards with 16k ledgers per shard. For example, 32k-ledgers
shard 1800 correnspond to 16k-ledgers shards with indexes 3600 and 3601 etc.

Here are result tables for these shards similar to tables from previous part.
In the first table we only take into consideration sizes of nudb.dat files.

|Index|32k-ledgers|16k-ledgers sum|Ratio|
|---|---|---|---|
|1300|4.00|2.39 + 2.32 = 4.71|1.18|
|1350|5.23|2.95 + 3.02 = 5.97|1.14|
|1400|4.37|2.53 + 2.59 = 5.12|1.17|
|1450|7.02|3.83 + 3.98 = 7.81|1.11|
|1500|7.53|4.49 + 3.86 = 8.35|1.11|
|1550|6.85|3.79 + 3.89 = 7.68|1.12|
|1600|7.28|4.15 + 3.99 = 8.14|1.12|
|1650|8.10|5.19 + 3.76 = 8.95|1.10|
|1700|7.58|4.18 + 4.27 = 8.45|1.11|
|1750|8.95|5.06 + 4.77 = 9.83|1.10|
|1800|7.29|4.18 + 4.02 = 8.20|1.12|
|Average|6.75|3.88 + 3.68 = 7.56|1.12|

In the second table we take into consideration total sizes of files nudb.dat
and nudb.key.

|Index|32k-ledgers|16k-ledgers sum|Ratio|
|---|---|---|---|
|1300|4.59|2.76 + 2.68 = 5.44|1.19|
|1350|5.98|3.40 + 3.47 = 6.87|1.15|
|1400|4.99|2.91 + 2.98 = 5.89|1.18|
|1450|8.02|4.40 + 4.56 = 8.96|1.12|
|1500|8.51|5.09 + 4.39 = 9.48|1.11|
|1550|7.73|4.29 + 4.42 = 8.71|1.13|
|1600|8.20|4.69 + 4.52 = 9.21|1.12|
|1650|9.20|5.92 + 4.29 = 10.21|1.11|
|1700|8.61|4.77 + 4.87 = 9.64|1.12|
|1750|10.09|5.73 + 5.41 = 11.14|1.10|
|1800|8.27|4.77 + 4.59 = 9.36|1.13|
|Average|7.69|4.43 + 4.20 = 8.63|1.12|

## Conclusion

We showed that using shards with 8k ledgers leads to raising required disk size
by 22% in comparison with using shards with 16k ledgers. In the same way,
using shards with 16k ledgers leads to raising required disk space by 12%
in comparison with using shards with 32k ledgers. Note that increase ratio 12%
is much less than 22% so using 32k-ledgers shards will bring us not so much
economy in disk space.

At the same time, size is one thing to compare but there are other aspects.
Smaller shards have an advantage that they take less time to acquire and
finalize. They also make for smaller archived shards which take less time to
download and import. Having more/smaller shards might also lead to better
database concurrency/performance.

It is hard to maintain both size and time parameters by a single optimization
formulae because different choices for weights of size and time may lead to
different results. But using "common sense" arguments we can compare
16k-ledgers shards and 32k-ledgers as follows: using 32k-ledgers shards give us
12% advantage in size, and about 44% disadvantage in time, because average size
of 16k-ledgers shard is about 56% of average 32k-ledgers shard. At the same,
if we compare 16k-ledgers shards with 8k-ledgers, then the first has 22%
advantage in size and 39% disadvantage in time. So the balance of
advantages/disadvantages is better when we use 16k-ledgers shards.

Thus we recommend use shards with 16K ledgers.
