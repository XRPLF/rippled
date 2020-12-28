#!/usr/bin/env python
"""A script to test rippled in an infinite loop of start-sync-stop.

- Expects 'rippled' to be on the `PATH` and 'rippled.cfg' to be in the current
  directory.
- Takes no arguments.
- Can be stopped with SIGINT.
- Tested on Python 3.8.
- Has no dependencies outside the standard library.
"""

import asyncio
import contextlib
import json
import logging
import os
import subprocess
import time
import urllib.error
import urllib.request


@contextlib.asynccontextmanager
async def rippled(path='rippled', config='rippled.cfg'):
    """A context manager for a rippled process."""
    # Start the server.
    process = await asyncio.create_subprocess_exec(
        path,
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
        logging.info(f'asking rippled ({process.pid}) to stop')
        start = time.time()
        process.terminate()

        # Wait nicely.
        try:
            await asyncio.wait_for(process.wait(), 60)
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
            f'rippled stopped after {end - start} seconds with code {code}'
        )


async def sync(port, duration=60, interval=5):
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
        if state == 'full':
            stopwatch += interval


async def loop(path='rippled', config='rippled.cfg', port=5005):
    """
    Start-sync-stop rippled in an infinite loop.

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
        logging.info(target)
        async with rippled(path, config) as process:
            start = time.time()
            exited = asyncio.create_task(process.wait())
            synced = asyncio.create_task(sync(port))
            # Try to sync as long as the process is running.
            done, pending = await asyncio.wait(
                {exited, synced},
                return_when=asyncio.FIRST_COMPLETED,
            )
            if done == {exited}:
                code = exited.result()
                logging.warning(
                    f'server halted for unknown reason with code {code}'
                )
            else:
                assert done == {synced}
                assert synced.exception() is None
            end = time.time()
            logging.info(f'{end - start} seconds to sync')
        id += 1


logging.basicConfig(
    format='%(asctime)s %(levelname)-8s %(message)s',
    level=logging.INFO,
    datefmt='%Y-%m-%d %H:%M:%S',
)
asyncio.run(loop())
