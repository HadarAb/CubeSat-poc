import unittest
import zlib

from ground_station.protocol import (
    CRC,
    FRAME_START,
    HEADER,
    MAX_PAYLOAD_SIZE,
    PAYLOAD,
    Frame,
    FrameParser,
    MessageType,
    decode_payload,
    encode_frame,
    encode_request,
)


class ProtocolTests(unittest.TestCase):
    def test_request_is_header_crc_and_no_payload(self):
        encoded = encode_request(
            MessageType.STATUS,
            0x1234,
            frame_timestamp_ms=0x01020304,
        )
        self.assertEqual(HEADER.size, 11)
        self.assertEqual(len(encoded), HEADER.size + CRC.size)
        self.assertEqual(
            HEADER.unpack_from(encoded),
            (
                FRAME_START,
                MessageType.STATUS,
                0x1234,
                0x01020304,
                0,
            ),
        )

    def test_crc_covers_header_and_payload(self):
        encoded = encode_request(
            MessageType.BATTERY,
            7,
            frame_timestamp_ms=10,
        )
        received_crc = CRC.unpack_from(encoded, len(encoded) - CRC.size)[0]
        self.assertEqual(
            received_crc,
            zlib.crc32(encoded[:-CRC.size]) & 0xFFFFFFFF,
        )

    def test_request_can_carry_payload(self):
        encoded = encode_request(
            MessageType.STATUS,
            8,
            b"arguments",
            frame_timestamp_ms=11,
        )
        frames = FrameParser().feed(encoded)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].sequence, 8)
        self.assertEqual(frames[0].payload, b"arguments")

    def test_parser_handles_noise_and_fragmented_frame(self):
        encoded = encode_request(
            MessageType.PAYLOAD,
            9,
            frame_timestamp_ms=123,
        )
        parser = FrameParser()
        self.assertEqual(parser.feed(b"noise" + encoded[:5]), [])
        self.assertEqual(parser.feed(encoded[5:12]), [])
        frames = parser.feed(encoded[12:])
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].sequence, 9)
        self.assertEqual(frames[0].msg_type, MessageType.PAYLOAD)
        self.assertEqual(frames[0].payload, b"")

    def test_parser_recovers_after_bad_crc(self):
        damaged = bytearray(
            encode_request(
                MessageType.STATUS,
                1,
                frame_timestamp_ms=0,
            )
        )
        damaged[-1] ^= 0x80
        good = encode_request(
            MessageType.BATTERY,
            2,
            frame_timestamp_ms=0,
        )
        parser = FrameParser()
        frames = parser.feed(bytes(damaged) + good)
        self.assertEqual(parser.crc_error_count, 1)
        self.assertEqual([frame.sequence for frame in frames], [2])

    def test_parser_rejects_oversized_length_and_resynchronizes(self):
        invalid_header = HEADER.pack(
            FRAME_START,
            MessageType.STATUS,
            1,
            0,
            MAX_PAYLOAD_SIZE + 1,
        )
        good = encode_request(
            MessageType.STATUS,
            3,
            frame_timestamp_ms=0,
        )
        parser = FrameParser()
        frames = parser.feed(invalid_header + good)
        self.assertEqual(parser.length_error_count, 1)
        self.assertEqual([frame.sequence for frame in frames], [3])

    def test_decode_empty_payload(self):
        wire_payload = PAYLOAD.pack(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        decoded = decode_payload(
            Frame(MessageType.PAYLOAD, 1, 2, wire_payload)
        )
        self.assertFalse(decoded.valid)
        self.assertEqual(decoded.status, 1)
        self.assertEqual(decoded.battery_pct, 0)
        self.assertEqual(decoded.temperature_c_x10, 0)

    def test_decode_valid_payload(self):
        wire_payload = PAYLOAD.pack(
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
        decoded = decode_payload(
            Frame(MessageType.PAYLOAD, 4, 5, wire_payload)
        )
        self.assertTrue(decoded.valid)
        self.assertEqual(decoded.node_id, 2)
        self.assertEqual(decoded.temperature_c_x10, 243)
        self.assertEqual(decoded.battery_pct, 87)
        self.assertEqual(decoded.i2c_success_count, 20)

    def test_payload_limit_is_enforced(self):
        with self.assertRaises(ValueError):
            encode_frame(
                MessageType.DEBUG_TEXT,
                1,
                b"x" * (MAX_PAYLOAD_SIZE + 1),
            )


if __name__ == "__main__":
    unittest.main()
