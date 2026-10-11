"""Event dispatch tests that do not load the native library."""

import asyncio
import unittest

from livekit.rtc._events import EventDispatcher


class EventDispatcherTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.dispatcher = EventDispatcher(asyncio.get_running_loop, capacity=2)

    async def test_queue_drops_oldest_and_dispatches_on_loop(self):
        seen = []
        self.dispatcher.on("value", seen.append)
        for value in (1, 2, 3):
            self.dispatcher.post("value", value)
        await asyncio.sleep(0)
        self.assertEqual(seen, [2, 3])
        self.assertEqual(self.dispatcher.dropped_events, 1)

    async def test_async_handler_and_close(self):
        seen = asyncio.Event()

        @self.dispatcher.on("value")
        async def handler(value):
            self.assertEqual(value, 1)
            seen.set()

        self.dispatcher.post("value", 1)
        await seen.wait()
        self.dispatcher.off("value", handler)
        self.dispatcher.post("value", 2)
        self.dispatcher.close()
        await asyncio.sleep(0)
        self.assertEqual(self.dispatcher.dropped_events, 0)


if __name__ == "__main__":
    unittest.main()
