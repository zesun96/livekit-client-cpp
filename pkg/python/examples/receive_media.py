"""Manually subscribe and read remote audio/video frames for a fixed duration."""

import asyncio

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def main():
    args = connection_parser(__doc__, duration=15).parse_args()
    async with rtc.Room() as room:
        subscribed = asyncio.Queue()
        room.on("track_subscribed", lambda publication, participant:
                subscribed.put_nowait((publication, participant)))
        room.on("track_published", lambda publication, participant:
                asyncio.create_task(room.set_remote_track_subscribed(
                    participant.sid, publication.sid, True)))
        await room.connect(args.url, args.token, rtc.RoomOptions(auto_subscribe=False))
        await wait_connected(room)
        # Include tracks already present when this participant joined.
        for participant in room.remote_participants.values():
            for publication in participant.track_publications.values():
                await room.set_remote_track_subscribed(participant.sid, publication.sid, True)

        async def consume(publication, participant):
            if publication.kind == 1:
                stream = await room.create_audio_stream(participant.identity, publication.sid)
            elif publication.kind == 2:
                stream = await room.create_video_stream(participant.identity, publication.sid)
            else:
                return
            frames = 0
            try:
                while room.is_connected:
                    frame = await stream.read(timeout_ms=200)
                    if frame is not None:
                        frames += 1
                        if frames == 1:
                            print(f"First frame {publication.sid}: {len(frame.data)} bytes")
            finally:
                stream.close()
                print(f"{publication.sid}: {frames} frames")

        tasks = []
        async def dispatch():
            while True:
                publication, participant = await subscribed.get()
                tasks.append(asyncio.create_task(consume(publication, participant)))

        dispatch_task = asyncio.create_task(dispatch())
        try:
            await run_for(args.seconds)
        finally:
            failure = (dispatch_task.exception() if dispatch_task.done() and
                       not dispatch_task.cancelled() else None)
            dispatch_task.cancel()
            await asyncio.gather(dispatch_task, return_exceptions=True)
            for task in tasks:
                task.cancel()
            results = await asyncio.gather(*tasks, return_exceptions=True)
            for result in results:
                if isinstance(result, Exception):
                    raise result
            if failure is not None:
                raise failure


if __name__ == "__main__":
    asyncio.run(main())
