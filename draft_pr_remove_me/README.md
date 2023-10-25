This directory will be removed when the PR is no long a "draft" pr. It contains some plots to compare the memory usage in this patch with the tip of develop as well as some scripts to create those plots. The scripts are tailored to my own setup, but should be straightforward to modify for other people.

The script `memstats2` collects memory statistics on all running rippled processes, and outputs a file with a head that includes information about the processes and data with columns for: process id, time (in seconds), size of resident memory (in gigabytes), inner node counts, and treenode cache size. The script assumes it is running on a linux system (it uses the `/proc` filesystem) and assumes the `jq` program is available (to parse json responses from rippled).

The script `memstats.py` is a python program to create plots from the data collected from the `memstat2` script.

The remaining files are the plots created with `memstats.py` from an overnight run. Two rippleds were started simultaneously on my development machine and used identical config files (other than the location of the database files). The script `memstats2` was started to collect data. After a 12 hour run, the script `memstats.py` was run to create the plots. There are three plots:

mem_usage_intrusive_ptr.png - This shows the size of resident memory for the two running rippled's.
mem_diff_intrusive_ptr.png - This shows the difference between the size of resident memory between the two running rippled (this shows the memory saving in gigabytes).
mem_percent_change.png - This shows the percent memory savings i.e. `100*(old_code_size-new_code_size)/old_code_size`

