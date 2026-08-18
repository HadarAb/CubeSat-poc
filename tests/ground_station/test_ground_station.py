import io
import unittest
from contextlib import redirect_stdout

from ground_station.ground_station import GroundStation
from ground_station.protocol import Frame, MessageType, PAYLOAD


class GroundStationRoutingTests(unittest.TestCase):
    def setUp(self):
        self.station = GroundStation(serial_port=None)
        self.wire_payload = PAYLOAD.pack(
            1,
            0,
            2,
            1,
            1000,
            243,
            512,
            18,
            87,
            20,
            3,
        )

    def test_automatic_status_is_displayed_and_not_queued(self):
        frame = Frame(MessageType.AUTO_STATUS, 7, 1000, self.wire_payload)

        output = io.StringIO()
        with redirect_stdout(output):
            self.station._dispatch_frame(frame)

        self.assertIn("[AUTO STATUS]", output.getvalue())
        self.assertIn("node=0x02", output.getvalue())
        self.assertTrue(self.station.response_queue.empty())

    def test_requested_status_is_queued_for_request(self):
        frame = Frame(MessageType.STATUS, 8, 1000, self.wire_payload)

        self.station._dispatch_frame(frame)

        self.assertIs(self.station.response_queue.get_nowait(), frame)


if __name__ == "__main__":
    unittest.main()
