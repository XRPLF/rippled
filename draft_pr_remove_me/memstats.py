#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Mon Oct 23 13:18:42 2023

@author: swd

Analyze the memstats2 output
"""

import pandas as pd
import seaborn as sns
from scipy.signal import medfilt
import matplotlib.pyplot as plt

data_dir= "/home/swd/memstats/data/nov_5"

def raw_file_to_df(data_file_name):
    n_to_skip = 0
    mine_pid = 0
    with open(data_file_name, 'r') as file:
        for line in file:
            if (line.startswith('pid')):
                break
            n_to_skip += 1
            if 'projs/ripple/mine' in line:
                mine_pid = int(line.split()[0])
    df = pd.read_csv(data_file_name, header=0,
                     delimiter=r'\s+', skiprows=n_to_skip)
    df['branch'] = 'dev'
    df.loc[df['pid'] == mine_pid, 'branch'] = 'intr_ptr'
    df['uptime_min'] = (df['time'] - df['time'].iloc[0])/60.0
    df['uptime_hr'] = df['uptime_min']/60

    return df


def get_timescale(df):
    if df['uptime_hr'].iloc[-1] < 5:
        return 'uptime_min', 'min'
    return 'uptime_hr', 'hrs'


def plot_df(df, ignore_min=30):

    x_col, units = get_timescale(df)
    y_col = 'res_gb'

    sns.set(style="whitegrid")
    sns.relplot(kind='line', data=df[df['uptime_min']
                > ignore_min], x=x_col, y=y_col, hue='branch')

    plt.xlabel(f'Up Time ({units})')
    plt.ylabel('Resident (gb)')
    plt.title('Memory Usage Intrusive Pointer')
    plt.subplots_adjust(top=0.9)
    plt.savefig(f"{data_dir}/mem_usage_intrusive_ptr.png")
    plt.show()


def plot_diff(df, filtered=True, ignore_min=30):
    x_col, units = get_timescale(df)
    diff = pd.DataFrame()
    diff['diff'] = df[df['branch'] == 'dev']['res_gb'].values - \
        df[df['branch'] == 'intr_ptr']['res_gb'].values
    if filtered:
        window_size = 11
        diff['filtered_diff'] = medfilt(diff['diff'], kernel_size=window_size)
    diff['uptime'] = df[df['branch'] == 'dev'][x_col].values
    diff['uptime_min'] = df[df['branch'] == 'dev']['uptime_min'].values
    y_column = 'diff' if not filtered else 'filtered_diff'

    sns.set(style="whitegrid")
    sns.relplot(
        kind='line', data=diff[diff['uptime_min'] > ignore_min], x='uptime', y=y_column)

    plt.xlabel(f'Up Time ({units})')
    plt.ylabel('Delta (gb)')
    title = 'Memory Difference Intrusive Pointer'
    if filtered:
        title += ' (filtered)'
    plt.title(title)

    plt.subplots_adjust(top=0.9)
    plt.savefig(f"{data_dir}/mem_diff_intrusive_ptr.png")
    plt.show()


def plot_percent_change(df, filtered=True, ignore_min=30):
    x_col, units = get_timescale(df)
    diff = pd.DataFrame()
    col_name = '% change'
    diff[col_name] = 100*(df[df['branch'] == 'dev']['res_gb'].values -
                          df[df['branch'] == 'intr_ptr']['res_gb'].values) / \
        df[df['branch'] == 'dev']['res_gb'].values
    if filtered:
        window_size = 11
        diff['filtered_' +
             col_name] = medfilt(diff[col_name], kernel_size=window_size)
    diff['uptime'] = df[df['branch'] == 'dev'][x_col].values
    diff['uptime_min'] = df[df['branch'] == 'dev']['uptime_min'].values
    y_column = col_name
    if filtered:
        y_column = 'filtered_'+col_name

    sns.set(style="whitegrid")
    sns.relplot(
        kind='line', data=diff[diff['uptime_min'] > ignore_min], x='uptime', y=y_column)

    plt.xlabel(f'Up Time ({units})')
    plt.ylabel('% Change (delta/old)')
    title = 'Percent Change Memory Intrusive Pointer'
    if filtered:
        title += ' (filtered)'
    plt.title(title)

    plt.subplots_adjust(top=0.9)
    plt.savefig(f"{data_dir}/mem_percent_change.png")
    plt.show()

def doit():
    data_file_name = f"{data_dir}/data.raw"
    df = raw_file_to_df(data_file_name)
    plot_df(df)
    plot_diff(df, filtered=True)
    plot_percent_change(df, filtered=True)
