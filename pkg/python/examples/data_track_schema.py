"""Store a JSON schema and publish a matching DataTrack frame.

The server must enable participant data blobs for schema storage.
"""

import asyncio

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def main():
    args = connection_parser(__doc__).parse_args()
    schema_id = rtc.DataTrackSchemaId(
        "example.telemetry.v1", rtc.DataTrackSchemaEncoding.JSON_SCHEMA,
    )
    schema = rtc.DataTrackSchema(
        schema_id, b'{"type":"object","properties":{"temperature":{"type":"number"}}}',
    )
    async with rtc.Room() as room:
        await room.connect(args.url, args.token)
        await wait_connected(room)
        await room.store_data_track_schema(schema)
        track = await room.publish_data_track(rtc.DataTrackPublishOptions(
            "example-telemetry", frame_encoding=rtc.DataTrackFrameEncoding.JSON,
            schema=schema_id,
        ))
        try:
            await run_for(0.5)  # Allow data channel negotiation.
            await track.try_push(b'{"temperature":21.5}', user_timestamp=1)
            await run_for(1)
            print("Published schema-backed DataTrack frame")
        finally:
            await track.unpublish()


if __name__ == "__main__":
    asyncio.run(main())
