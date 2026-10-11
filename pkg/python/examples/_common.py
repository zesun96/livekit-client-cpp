"""Small command-line helpers shared by the standalone examples."""

import argparse
import asyncio
import os


def connection_parser(description: str, *, duration: float | None = None):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--url", default=os.getenv("LIVEKIT_URL"), required=not os.getenv("LIVEKIT_URL"))
    parser.add_argument("--token", default=os.getenv("LIVEKIT_TOKEN"),
                        required=not os.getenv("LIVEKIT_TOKEN"))
    if duration is not None:
        parser.add_argument("--seconds", type=float, default=duration)
    return parser


async def run_for(seconds: float):
    if seconds < 0:
        raise ValueError("seconds must be nonnegative")
    await asyncio.sleep(seconds)


async def wait_connected(room, timeout: float = 10):
    async def wait():
        while not room.is_connected:
            await asyncio.sleep(0.05)
    await asyncio.wait_for(wait(), timeout)
