#!/usr/bin/env python
"""A script to test rippled in an infinite loop of start-sync-stop.

- Can be stopped with SIGINT.
- Tested on Python 3.8.
- Has no dependencies outside the standard library.
"""

import argparse
import asyncio
import contextlib
import json
import logging
import os
from pathlib import Path
import subprocess
import time
import urllib.error
import urllib.request

DEFAULT_EXE = 'rippled'
DEFAULT_CONFIG = 'rippled.cfg'
DEFAULT_PORT = 5005
# Number of seconds to wait before forcefully terminating.
PATIENCE = 120
# Number of contiguous seconds in a sync state to be considered synced.
DEFAULT_SYNC_DURATION = 60
# Number of seconds between polls of state.
DEFAULT_POLL_INTERVAL = 5
SYNC_STATES = ('full', 'validating', 'proposing')


@contextlib.asynccontextmanager
async def rippled(exe=DEFAULT_EXE, config=DEFAULT_CONFIG):
    """A context manager for a rippled process."""
    # Start the server.
    process = await asyncio.create_subprocess_exec(
        exe,
        '--conf',
        config,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
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
    stopwatch = 0
    while stopwatch < duration:
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
                logging.warning(f'server_state returned s.o.t. JSON: {cause}')
                stopwatch = 0
                continue

        try:
            state = body['result']['state']['server_state']
        except KeyError as cause:
            logging.warning(f'server_state response missing key: {cause.key}')
            stopwatch = 0
            continue
        logging.info(f'server_state: {state}')
        if state in SYNC_STATES:
            stopwatch += interval
        else:
            # Require a contiguous sync state.
            stopwatch = 0


async def loop(test, *, exe=DEFAULT_EXE, config=DEFAULT_CONFIG):
    """
    Start-test-stop rippled in an infinite loop.

    Logs to a different file for each iteration.
    """
    id = 0
    while True:
        link = 'debug.log'
        target = f'debug.{id}.log'
        try:
            os.unlink(link)
            os.unlink(target)
        except FileNotFoundError:
            pass
        os.symlink(target, link)
        logging.info(f'iteration {id} logging to {target}')
        async with rippled(exe, config) as process:
            start = time.time()
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
            end = time.time()
            logging.info(f'synced after {end - start:.0f} seconds')
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
    default=DEFAULT_CONFIG,
    help='Path to configuration file.',
)
parser.add_argument(
    '--port',
    type=int,
    default=DEFAULT_PORT,
    help='The port chosen for JSON RPC.',
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
args = parser.parse_args()


def test():
    return sync(args.port, duration=args.duration, interval=args.interval)


asyncio.run(loop(test, exe=args.rippled, config=args.conf))
