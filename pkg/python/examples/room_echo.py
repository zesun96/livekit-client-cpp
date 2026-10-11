"""Join a LiveKit room and print incoming data and participant events.

Set LIVEKIT_URL and LIVEKIT_TOKEN or pass --url and --token.
"""

import asyncio

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def main() -> None:
    args = connection_parser(__doc__, duration=10).parse_args()
    async with rtc.Room() as room:
        room.on("participant_connected", lambda participant:
                print(f"joined: {participant.identity}"))
        room.on("data_received", lambda packet:
                print(f"data from {packet.participant_identity}: {packet.data!r}"))
        room.on("chat_message_received", lambda message:
                print(f"chat from {message.participant_identity}: {message.message}"))

        await room.connect(args.url, args.token)
        await wait_connected(room)
        print(f"connected: room={room.name} identity={room.local_participant.identity}")
        await room.local_participant.publish_data(b"hello from Python", topic="greeting")
        await run_for(args.seconds)


if __name__ == "__main__":
    asyncio.run(main())
