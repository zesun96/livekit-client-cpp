"""Publish generated I420 color frames for five seconds; no device required."""

import asyncio

from livekit import rtc
from _common import connection_parser, wait_connected


WIDTH, HEIGHT = 160, 90


def frame(phase):
    # I420: full-size Y plane followed by two quarter-size chroma planes.
    luma = bytes([40 + phase % 180]) * (WIDTH * HEIGHT)
    chroma = bytes([128]) * (WIDTH * HEIGHT // 4)
    return luma + chroma + chroma


async def main():
    args = connection_parser(__doc__, duration=5).parse_args()
    source = rtc.VideoSource()
    try:
        async with rtc.Room() as room:
            await room.connect(args.url, args.token)
            await wait_connected(room)
            await source.capture_i420(frame(0), WIDTH, HEIGHT)
            track = await room.local_participant.create_video_track("python-video", source)
            await track.publish(rtc.TrackPublishOptions(source=rtc.TrackSource.CAMERA))
            for index in range(int(args.seconds * 30)):
                await source.capture_i420(frame(index), WIDTH, HEIGHT)
                await asyncio.sleep(1 / 30)
            print(f"Published {args.seconds:g} seconds of synthetic video")
    finally:
        source.close()


if __name__ == "__main__":
    asyncio.run(main())
