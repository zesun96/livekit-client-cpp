"""Native loader errors independent of installed SDK binaries."""

import os
import unittest
from unittest.mock import patch

from livekit.rtc._native import LiveKitError, _load_library


class NativeLoaderTests(unittest.TestCase):
    def test_missing_explicit_library_has_actionable_error(self):
        with patch.dict(os.environ, {
            "LIVEKIT_CLIENT_CPP_LIBRARY": "missing-livekitclient-library.dll"
        }):
            with self.assertRaisesRegex(LiveKitError, "could not load livekitclient"):
                _load_library()


if __name__ == "__main__":
    unittest.main()
