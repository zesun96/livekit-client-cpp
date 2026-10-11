"""Publish a real microphone for ten seconds; opens an input device."""

import asyncio
from _publish_device import publish_device


if __name__ == "__main__":
    asyncio.run(publish_device("microphone"))
