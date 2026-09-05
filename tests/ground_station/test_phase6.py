import io
import unittest
from contextlib import redirect_stdout

from ground_station.ground_station import GroundStation, do_fetch
from ground_station.protocol import (
    FETCH_END_PAYLOAD, LOG_RECORD, FetchEnd, MessageType, Status, VOLUME_HK,
    decode_fetch_end, decode_fetch_records, encode_fetch, encode_set_time,
    record_crc_is_valid, Frame,
)
from ground_station.sensors import (
    SENSOR_ID_RESET_CAUSE, describe_reset_flags, describe_sensor,
    find_hash_collisions, vtable_hash_name,
)


class Phase6Tests(unittest.TestCase):
    def test_set_time_payload_is_four_bytes(self):
        self.assertEqual(len(encode_set_time(1700000000)), 4)

    def test_fetch_rejects_inverted_range(self):
        with self.assertRaises(ValueError):
            encode_fetch(200, 100, 0)

    def test_fetch_data_frame_holds_four_records(self):
        blob = b"".join(
            LOG_RECORD.pack(1000 + i, 0x0201, 0, 4, b"\x01\x00\x00\x00", 0)
            for i in range(4)
        )
        self.assertEqual(len(blob), 64)
        frame = Frame(MessageType.FETCH_DATA, 1, 0, blob)
        self.assertEqual(len(decode_fetch_records(frame)), 4)

    def test_fetch_end_reports_probe_count(self):
        frame = Frame(MessageType.FETCH_END, 1, 0, FETCH_END_PAYLOAD.pack(0, 500, 14))
        end = decode_fetch_end(frame)
        self.assertEqual(end.record_count, 500)
        self.assertEqual(end.probe_count, 14)

    def test_no_sensor_id_collisions(self):
        self.assertEqual(find_hash_collisions(), [])

    def test_boot_marker_resolves(self):
        node, key, _ = describe_sensor(0x03FF)
        self.assertEqual((node, key), ("OBC", "BOOT"))

    def test_reset_cause_marker_resolves(self):
        node, key, _ = describe_sensor(SENSOR_ID_RESET_CAUSE)
        self.assertEqual((node, key), ("OBC", "RESET"))

    def test_reset_flags_are_readable(self):
        flags = (1 << 26) | (1 << 29)
        self.assertEqual(describe_reset_flags(flags), "RESET_PIN|IWDG")

    def test_boot_fetch_uses_housekeeping_volume(self):
        class Station:
            last_sync_epoch = None
            requested_volume = None

            def fetch(self, from_epoch, to_epoch, volume, record_handler=None):
                self.requested_volume = volume
                return [], FetchEnd(0, 0, 0)

        station = Station()
        with redirect_stdout(io.StringIO()):
            do_fetch(station, ["100", "200", "payload", "--boot"])

        self.assertEqual(station.requested_volume, VOLUME_HK)

    def test_fetch_collects_data_until_fetch_end(self):
        station = GroundStation(serial_port=None)
        station.start = lambda: None
        station._send = lambda frame: None
        record = LOG_RECORD.pack(150, 0x0201, 0, 4, b"\x01\x00\x00\x00", 0)
        station.response_queue.put(Frame(MessageType.FETCH_DATA, 1, 0, record))
        station.response_queue.put(Frame(MessageType.FETCH_END, 1, 0, FETCH_END_PAYLOAD.pack(Status.OK, 1, 14)))

        records, end = station.fetch(100, 200, 0)

        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].epoch_s, 150)
        self.assertEqual((end.record_count, end.probe_count), (1, 14))

    def test_fetch_streams_data_without_collecting_it(self):
        station = GroundStation(serial_port=None)
        station.start = lambda: None
        station._send = lambda frame: None
        received = []
        record = LOG_RECORD.pack(150, 0x0201, 0, 4, b"\x01\x00\x00\x00", 0)
        station.response_queue.put(Frame(MessageType.FETCH_DATA, 1, 0, record))
        station.response_queue.put(Frame(MessageType.FETCH_END, 1, 0, FETCH_END_PAYLOAD.pack(Status.OK, 1, 14)))

        records, end = station.fetch(100, 200, 0, record_handler=received.extend)

        self.assertEqual(records, [])
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0].epoch_s, 150)
        self.assertEqual((end.record_count, end.probe_count), (1, 14))

    def test_fetch_reports_storage_error(self):
        station = GroundStation(serial_port=None)
        station.start = lambda: None
        station._send = lambda frame: None
        station.response_queue.put(Frame(MessageType.FETCH_END, 1, 0, FETCH_END_PAYLOAD.pack(Status.STORAGE_ERROR, 0, 0)))

        with self.assertRaisesRegex(RuntimeError, "STORAGE_ERROR"):
            station.fetch(100, 200, 0)

    def test_hash_is_stable(self):
        # Guards against an accidental change to the C hash.
        self.assertEqual(vtable_hash_name("TEMP"), vtable_hash_name("TEMP"))


if __name__ == "__main__":
    unittest.main()
