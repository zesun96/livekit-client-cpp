"""Publish a generated 440 Hz tone for five seconds; no device required."""

import asyncio
import math
from array import array

from livekit import rtc
from _common import connection_parser, wait_connected


async def main():
    args = connection_parser(__doc__, duration=5).parse_args()
    source = rtc.AudioSource(sample_rate=48000, num_channels=1, queue_size_ms=200)
    try:
        async with rtc.Room() as room:
            await room.connect(args.url, args.token)
            await wait_connected(room)
            await source.capture_frame(bytes(960))  # Prime the source before publication.
            track = await room.local_participant.create_audio_track("python-tone", source)
            await track.publish(rtc.TrackPublishOptions(source=rtc.TrackSource.MICROPHONE))
            for frame in range(int(args.seconds * 100)):
                samples = array("h", (
                    int(6000 * math.sin(2 * math.pi * 440 * (frame * 480 + i) / 48000))
                    for i in range(480)
                ))
                await source.capture_frame(samples.tobytes())
                await asyncio.sleep(0.01)
            print(f"Published {args.seconds:g} seconds of synthetic audio")
    finally:
        source.close()


if __name__ == "__main__":
    asyncio.run(main())
