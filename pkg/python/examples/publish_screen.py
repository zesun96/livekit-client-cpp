"""Publish a monitor capture for ten seconds; opens a screen source."""

import asyncio
from _publish_device import publish_device


if __name__ == "__main__":
    asyncio.run(publish_device("screen"))
