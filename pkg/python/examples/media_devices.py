"""List capture devices and screen sources without opening them."""

from livekit import rtc


def main():
    kinds = {0: "audio input", 1: "audio output", 2: "video input"}
    for device in rtc.list_media_devices():
        default = " [default]" if device.is_default else ""
        print(f"{kinds.get(device.kind, 'unknown')}{default}: {device.label}\n  {device.id}")
    for source in rtc.list_screen_sources():
        print(f"screen kind={source.kind}: {source.label} ({source.width}x{source.height})\n  {source.id}")


if __name__ == "__main__":
    main()
