"""Shared implementation for the four opt-in physical-capture examples."""

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def publish_device(kind: str):
    parser = connection_parser(f"Publish {kind} capture", duration=10)
    parser.add_argument("--device-id", default="", help="audio/camera device or screen source ID")
    args = parser.parse_args()
    if kind == "microphone":
        source = rtc.AudioSource.from_microphone(args.device_id)
        label, track_source = "microphone", rtc.TrackSource.MICROPHONE
    elif kind == "system_audio":
        source = rtc.AudioSource.from_system_audio(args.device_id)
        label, track_source = "system-audio", rtc.TrackSource.SCREEN_SHARE_AUDIO
    elif kind == "camera":
        source = rtc.VideoSource.from_camera(args.device_id)
        label, track_source = "camera", rtc.TrackSource.CAMERA
    elif kind == "screen":
        source_id = args.device_id
        if not source_id:
            source_id = next((item.id for item in rtc.list_screen_sources()
                              if item.kind == 0), "")
        if not source_id:
            raise RuntimeError("no screen source found; pass --device-id")
        source = rtc.VideoSource.from_screen(source_id, frames_per_second=15)
        label, track_source = "screen", rtc.TrackSource.SCREEN_SHARE
    else:
        raise ValueError(kind)

    try:
        async with rtc.Room() as room:
            await room.connect(args.url, args.token)
            await wait_connected(room)
            await source.start_capture()
            await run_for(0.5)  # Wait for the first captured frame before publishing.
            if isinstance(source, rtc.AudioSource):
                track = await room.local_participant.create_audio_track(label, source)
            else:
                track = await room.local_participant.create_video_track(label, source)
            options = rtc.TrackPublishOptions(source=track_source)
            if kind == "screen":
                await track.publish_screen_share_video(options)
            elif kind == "system_audio":
                await track.publish_screen_share_audio(options)
            else:
                await track.publish(options)
            print(f"Publishing {kind} from {args.device_id or 'default'}")
            await run_for(args.seconds)
            if kind == "microphone":
                print("Processed microphone frames:", source.processing_stats.capture_frames_processed)
    finally:
        try:
            await source.stop_capture()
        finally:
            source.close()
