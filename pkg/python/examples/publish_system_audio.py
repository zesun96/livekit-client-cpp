"""Publish system output loopback for ten seconds; opens an audio device."""

import asyncio
from _publish_device import publish_device


if __name__ == "__main__":
    asyncio.run(publish_device("system_audio"))
