"""Record subscribed remote audio to WAV and video to codec-specific files."""

import asyncio
from pathlib import Path

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def main():
    parser = connection_parser(__doc__, duration=15)
    parser.add_argument("output_dir", type=Path, help="existing output directory")
    args = parser.parse_args()
    if not args.output_dir.is_dir():
        parser.error("output_dir must be an existing directory")
    async with rtc.Room() as room:
        subscribed = asyncio.Queue()
        room.on("track_subscribed", lambda publication, participant:
                subscribed.put_nowait((publication, participant)))
        await room.connect(args.url, args.token)
        await wait_connected(room)
        recorders = []

        async def dispatch():
            while True:
                publication, participant = await subscribed.get()
                if publication.kind not in (1, 2):
                    continue
                # Native recorder chooses .h264, .h265, or .ivf for video.
                suffix = ".wav" if publication.kind == 1 else ""
                output = args.output_dir / (publication.sid + suffix)
                recorder = await room.start_track_recording(
                    participant.identity, publication.sid, str(output))
                recorders.append(recorder)
                print("Recording", publication.sid, "to", output)

        task = asyncio.create_task(dispatch())
        try:
            await run_for(args.seconds)
        finally:
            failure = task.exception() if task.done() and not task.cancelled() else None
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            for recorder in recorders:
                await recorder.stop()
                stats = recorder.stats
                print(f"{stats.output_path}: {stats.frames_written} frames, "
                      f"{stats.bytes_written} bytes, {stats.frames_dropped} dropped")
                await recorder.aclose()
            if failure is not None:
                raise failure


if __name__ == "__main__":
    asyncio.run(main())
