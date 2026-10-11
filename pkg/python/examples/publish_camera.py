"""Publish a real camera for ten seconds; opens a video device."""

import asyncio
from _publish_device import publish_device


if __name__ == "__main__":
    asyncio.run(publish_device("camera"))
