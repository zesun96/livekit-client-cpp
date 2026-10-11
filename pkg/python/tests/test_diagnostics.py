"""Process-wide diagnostic controls and borrowed callback values."""

import ctypes as C
import os
import unittest

from livekit import rtc
from livekit.rtc import diagnostics
from livekit.rtc._native import LogRecordRaw, TraceRecordRaw


@unittest.skipUnless(os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY"), "native library is not set")
class DiagnosticsTests(unittest.TestCase):
    def test_options_round_trip(self):
        levels = rtc.get_log_levels()
        enabled, categories = rtc.get_trace_options()
        try:
            rtc.set_log_levels(*levels)
            self.assertEqual(rtc.get_log_levels(), levels)
            rtc.set_trace_options(enabled=True, categories=rtc.TraceCategory.RPC)
            self.assertEqual(rtc.get_trace_options(), (True, rtc.TraceCategory.RPC))
            with self.assertRaises(ValueError):
                rtc.set_trace_options(enabled=True, categories=1 << 20)
        finally:
            rtc.set_log_levels(*levels)
            rtc.set_trace_options(enabled=enabled, categories=categories)

    def test_borrowed_callbacks_are_copied(self):
        logs, traces = [], []
        try:
            rtc.set_log_callback(logs.append)
            rtc.set_trace_callback(traces.append)
            log = LogRecordRaw(C.sizeof(LogRecordRaw), 2, 0, b"message", b"file.cc", 42)
            trace = TraceRecordRaw(C.sizeof(TraceRecordRaw), 0, 1, b"connect", 1, 2, 3)
            diagnostics._log_callback(None, C.byref(log))
            diagnostics._trace_callback(None, C.byref(trace))
            self.assertEqual(logs[0].message, "message")
            self.assertEqual(traces[0].name, "connect")
        finally:
            rtc.set_log_callback(None)
            rtc.set_trace_callback(None)
