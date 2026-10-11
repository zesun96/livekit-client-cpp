"""Receive decoded remote media through Room event callbacks."""

import asyncio

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def main():
    args = connection_parser(__doc__, duration=15).parse_args()
    counts = {}

    def on_frame(publication, participant, frame):
        count = counts.get(publication.sid, 0) + 1
        counts[publication.sid] = count
        if count == 1:
            print(f"First frame from {participant.identity} / {publication.sid}: "
                  f"{len(frame.data)} bytes")

    async with rtc.Room() as room:
        room.on("audio_frame", on_frame)
        room.on("video_frame", on_frame)
        await room.connect(args.url, args.token)
        await wait_connected(room)
        await run_for(args.seconds)
        for sid, count in counts.items():
            print(f"{sid}: {count} frames")
        print("Dropped room events:", room.dropped_events)


if __name__ == "__main__":
    asyncio.run(main())
