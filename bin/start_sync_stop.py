#!/usr/bin/env python
"""A script to test rippled in an infinite loop of start-sync-stop.

- Requires Python 3.7+.
- Can be stopped with SIGINT.
- Has no dependencies outside the standard library.
"""

import sys

assert sys.version_info.major == 3 and sys.version_info.minor >= 7

import argparse
import asyncio
import configparser
import contextlib
import json
import logging
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time
import urllib.error
import urllib.request

# Enable asynchronous subprocesses on Windows. The default changed in 3.8.
# https://docs.python.org/3.7/library/asyncio-platforms.html#subprocess-support-on-windows
if (platform.system() == 'Windows' and sys.version_info.major == 3
        and sys.version_info.minor < 8):
    asyncio.set_event_loop_policy(asyncio.WindowsProactorEventLoopPolicy())

DEFAULT_EXE = 'rippled'
DEFAULT_CONFIGURATION_FILE = 'rippled.cfg'
# Number of seconds to wait before forcefully terminating.
PATIENCE = 120
# Number of contiguous seconds in a sync state to be considered synced.
DEFAULT_SYNC_DURATION = 60
# Number of seconds between polls of state.
DEFAULT_POLL_INTERVAL = 5
DEFAULT_DATABASE_CLEAR = False
# The 'proposing' state does not yet have a copy of the ledger.
SYNC_STATES = ('full', 'validating')


def read_config(config_path):
    # strict = False: Allow duplicate keys, e.g. [rpc_startup].
    # allow_no_value = True: Allow keys with no values. Generally, these
    # instances use the "key" as the value, and the section name is the key,
    # e.g. [debug_logfile].
    # delimiters = ('='): Allow ':' as a character in Windows paths. Some of
    # our "keys" are actually values, and we don't want to split them on ':'.
    config = configparser.ConfigParser(
        strict=False,
        allow_no_value=True,
        delimiters=('='),
    )
    config.read(config_path)
    setattr(config, 'path', config_path)
    return config


def to_list(value, separator=','):
    """Parse a list from a delimited string value."""
    return [s.strip() for s in value.split(separator) if s]

def get_single_value(config, section):
    """Return the only value within the given section.
    Raises all kinds of exceptions if there is any ambiguity."""
    if section not in config:
        raise ValueError(
            f'no [{section}] in configuration file: {config.path}')
    values = list(config[section ].keys())
    if len(values) < 1:
        raise ValueError(
            f'no values under [{section}] in configuration file: {config.path}')
    if len(values) > 1:
        raise ValueError(
            f'too many values under [{section}] in configuration file: {config.path}')
    return values[0]

def find_log_file(config):
    return get_single_value(config, 'debug_logfile')

def find_database_path(config):
    return get_single_value(config, 'database_path')

def find_http_port(config):
    names = list(config['server'].keys())
    for name in names:
        server = config[name]
        if 'http' in to_list(server.get('protocol', '')):
            return int(server['port'])
    raise ValueError(f'no server in [server] for "http" protocol')


@contextlib.asynccontextmanager
async def rippled(exe=DEFAULT_EXE, config_path=DEFAULT_CONFIGURATION_FILE):
    """A context manager for a rippled process."""
    # Start the server.
    # The `[debug_logfile]` does not capture stderr,
    # which includes failed assertions.
    # Thus, we capture everything in a separate file.
    logfile = open('rippled.log', 'w')
    process = await asyncio.create_subprocess_exec(
        str(exe),
        '--conf',
        str(config_path),
        stdout=logfile,
        stderr=subprocess.STDOUT,
    )
    logging.info(f'rippled started with pid {process.pid}')
    try:
        yield process
    finally:
        # Ask it to stop.
        logging.info(f'asking rippled (pid: {process.pid}) to stop')
        start = time.time()
        process.terminate()

        # Wait nicely.
        try:
            await asyncio.wait_for(process.wait(), PATIENCE)
        except asyncio.TimeoutError:
            # Ask the operating system to kill it.
            logging.warning(f'killing rippled ({process.pid})')
            try:
                process.kill()
            except ProcessLookupError:
                pass

        code = await process.wait()
        end = time.time()
        logfile.close()
        logging.info(
            f'rippled stopped after {end - start:.1f} seconds with code {code}'
        )


async def sync(
        port,
        *,
        duration=DEFAULT_SYNC_DURATION,
        interval=DEFAULT_POLL_INTERVAL,
):
    """Poll rippled on an interval until it has been synced for a duration."""
    start = time.perf_counter()
    while (time.perf_counter() - start) < duration:
        await asyncio.sleep(interval)

        request = urllib.request.Request(
            f'http://127.0.0.1:{port}',
            data=json.dumps({
                'method': 'server_state'
            }).encode(),
            headers={'Content-Type': 'application/json'},
        )
        with urllib.request.urlopen(request) as response:
            try:
                body = json.loads(response.read())
            except urllib.error.HTTPError as cause:
                logging.warning(f'server_state returned not JSON: {cause}')
                start = time.perf_counter()
                continue

        try:
            state = body['result']['state']['server_state']
        except KeyError as cause:
            logging.warning(f'server_state response missing key: {cause.key}')
            start = time.perf_counter()
            continue
        logging.info(f'server_state: {state}')
        if state not in SYNC_STATES:
            # Require a contiguous sync state.
            start = time.perf_counter()


async def loop(
    test,
    *,
    exe=DEFAULT_EXE,
    config_path=DEFAULT_CONFIGURATION_FILE,
    clear=DEFAULT_DATABASE_CLEAR,
):
    """
    Start-test-stop rippled in an infinite loop.

    Moves log to a different file after each iteration.
    """
    shutil.rmtree('logs', ignore_errors=True)
    os.makedirs('logs')
    id = 0
    while True:
        logging.info(f'iteration: {id}')
        if clear:
            shutil.rmtree(the_database_path, ignore_errors=True)
        async with rippled(exe, config_path) as process:
            start = time.perf_counter()
            exited = asyncio.create_task(process.wait())
            tested = asyncio.create_task(test())
            # Try to sync as long as the process is running.
            done, pending = await asyncio.wait(
                {exited, tested},
                return_when=asyncio.FIRST_COMPLETED,
            )
            if done == {exited}:
                code = exited.result()
                logging.warning(
                    f'server halted for unknown reason with code {code}')
            else:
                assert done == {tested}
                assert tested.exception() is None
            end = time.perf_counter()
            logging.info(f'synced after {end - start:.0f} seconds')
        os.replace('rippled.log', f'logs/rippled.{id}.log')
        id += 1


logging.basicConfig(
    format='%(asctime)s %(levelname)-8s %(message)s',
    level=logging.INFO,
    datefmt='%Y-%m-%d %H:%M:%S',
)

parser = argparse.ArgumentParser(
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument(
    'rippled',
    type=Path,
    nargs='?',
    default=DEFAULT_EXE,
    help='Path to rippled.',
)
parser.add_argument(
    '--conf',
    type=Path,
    default=DEFAULT_CONFIGURATION_FILE,
    help='Path to configuration file.',
)
parser.add_argument(
    '--duration',
    type=int,
    default=DEFAULT_SYNC_DURATION,
    help='Number of contiguous seconds required in a synchronized state.',
)
parser.add_argument(
    '--interval',
    type=int,
    default=DEFAULT_POLL_INTERVAL,
    help='Number of seconds to wait between polls of state.',
)
parser.add_argument(
    '--clear',
    action='store_true',
    help='Whether to delete the database between each synchronization.',
)
args = parser.parse_args()

the_config = read_config(args.conf)
port = find_http_port(the_config)
the_database_path = find_database_path(the_config)



def test():
    return sync(port, duration=args.duration, interval=args.interval)


try:
    asyncio.run(loop(
        test,
        exe=args.rippled,
        config_path=args.conf,
        clear=args.clear,
    ))
except KeyboardInterrupt:
    # Squelch the message. This is a normal mode of exit.
    pass
