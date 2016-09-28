#/usr/bin/env python

# Script to read the result of the benchmark program and plot the results.
# Options:
#   `-i arg` : input file (benchmark result)
# Notes: After the script runs the plot will automatically be shown in matplotlib.
#        Tested with python 3 only.

import argparse
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

def run_main(result_filename):
    d = pd.read_csv(result_filename)
    p = sns.lmplot(x='num_db_items', y='ops/sec', data=d[d['num_db_items']>=500000], hue='db', col='op')
    plt.show(p)
    return d  # for testing


def parse_args():
    parser = argparse.ArgumentParser(
        description=('Plot the benchmark results'))
    parser.add_argument(
        '--input',
        '-i',
        help=('input'), )
    return parser.parse_args()


if __name__ == '__main__':
    args = parse_args()
    result_filename = args.input
    if not result_filename:
        print('No result file specified. Exiting')
    else:
        run_main(result_filename)
