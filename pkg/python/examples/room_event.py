"""Print room, participant, data, stream, and track events."""

import asyncio

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


EVENTS = (
    "connected", "disconnected", "reconnecting", "reconnected",
    "participant_connected", "participant_disconnected", "track_published",
    "track_unpublished", "track_subscribed", "track_unsubscribed",
    "track_subscription_failed", "track_subscription_status_changed",
    "track_stream_state_changed", "data_received", "chat_message_received",
    "text_received", "byte_received", "file_received", "text_stream_event",
    "byte_stream_event", "data_track_published", "data_track_unpublished",
)


async def main():
    args = connection_parser(__doc__, duration=30).parse_args()
    async with rtc.Room() as room:
        for name in EVENTS:
            room.on(name, lambda *values, event=name: print(event, *values))
        await room.connect(args.url, args.token)
        await wait_connected(room)
        print("Listening as", room.local_participant.identity)
        await run_for(args.seconds)


if __name__ == "__main__":
    asyncio.run(main())
