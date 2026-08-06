import struct
import unittest

from ground_station.protocol import Frame, FrameParser
from simulators.models import EpsModel, PayloadModel
from simulators.protocol import (
    SIM_ACK_PAYLOAD,
    SIM_SET_PAYLOAD,
    SimMessageType,
    SimStatus,
    SimValue,
    VtType,
    decode_sim_ack,
    decode_sim_set,
    encode_name,
    encode_sim_set,
)
from simulators.runtime import parse_sensor_selection


class SimulatorProtocolTests(unittest.TestCase):
    def test_sim_set_round_trip_uses_fixed_18_byte_payload(self):
        encoded = encode_sim_set(17, SimValue("VBAT", VtType.F32, 3.65))
        frames = FrameParser().feed(encoded)
        self.assertEqual(len(frames), 1)
        self.assertEqual(len(frames[0].payload), SIM_SET_PAYLOAD.size)
        self.assertEqual(SIM_SET_PAYLOAD.size, 18)
        decoded = decode_sim_set(frames[0])
        self.assertEqual(decoded.name, "VBAT")
        self.assertEqual(decoded.type, VtType.F32)
        self.assertAlmostEqual(decoded.value, 3.65, places=5)

    def test_eight_character_key_is_not_truncated(self):
        encoded = encode_sim_set(1, SimValue("ABCDEFGH", VtType.U32, 12))
        decoded = decode_sim_set(FrameParser().feed(encoded)[0])
        self.assertEqual(decoded.name, "ABCDEFGH")
        self.assertEqual(decoded.value, 12)

    def test_invalid_key_name_is_rejected(self):
        with self.assertRaises(ValueError):
            encode_name("TOO_LONG_NAME")
        with self.assertRaises(ValueError):
            encode_name("TEMP_C\N{DEGREE SIGN}")

    def test_ack_decodes_success_and_empty_error_value(self):
        success_payload = SIM_ACK_PAYLOAD.pack(
            SimStatus.OK,
            SimMessageType.SET,
            2,
            4,
            encode_name("SEL"),
            VtType.U32,
            4,
            struct.pack("<I", 7).ljust(8, b"\0"),
        )
        success = decode_sim_ack(Frame(SimMessageType.ACK, 1, 0, success_payload))
        self.assertEqual(success.name, "SEL")
        self.assertEqual(success.value.value, 7)

        error_payload = SIM_ACK_PAYLOAD.pack(
            SimStatus.UNKNOWN_KEY,
            SimMessageType.GET,
            0,
            0,
            encode_name("MISSING"),
            VtType.U32,
            0,
            bytes(8),
        )
        error = decode_sim_ack(Frame(SimMessageType.ACK, 2, 0, error_payload))
        self.assertEqual(error.status, SimStatus.UNKNOWN_KEY)
        self.assertEqual(error.name, "MISSING")
        self.assertIsNone(error.value)


class SimulationModelTests(unittest.TestCase):
    def test_payload_can_enable_one_or_multiple_sensor_groups(self):
        one = PayloadModel({"radiation"}, dose_rate=2.0, seed=1).step(0.5)
        self.assertEqual([item.name for item in one], ["TDOSE"])
        self.assertAlmostEqual(one[0].value, 1.0)

        multiple = PayloadModel({"temperature", "sel", "reset"}, seed=1).step(1.0)
        self.assertEqual([item.name for item in multiple], ["TEMP", "SEL", "NRESET"])

    def test_payload_commands_inject_events(self):
        model = PayloadModel({"sel", "reset"}, sel_rate_per_minute=0.0)
        model.apply_command(["sel", "3"])
        model.apply_command(["reset"])
        values = {item.name: item.value for item in model.step(0.0)}
        self.assertEqual(values["SEL"], 3)
        self.assertEqual(values["NRESET"], 1)

    def test_eps_drain_reduces_charge_and_voltage(self):
        model = EpsModel(
            {"battery"},
            capacity_ah=2.0,
            state_of_charge_pct=100.0,
            load_current_a=1.0,
            solar_current_a=0.0,
            seed=1,
        )
        before = model.voltage_v
        model.step(3600.0)
        self.assertAlmostEqual(model.state_of_charge_pct, 50.0)
        self.assertLess(model.voltage_v, before)

    def test_eps_voltage_can_be_overridden_and_restored(self):
        model = EpsModel({"battery"}, voltage_override=3.45)
        self.assertEqual(model.voltage_v, 3.45)
        model.apply_command(["voltage", "3.20"])
        self.assertEqual(model.voltage_v, 3.20)
        model.apply_command(["voltage", "auto"])
        self.assertIsNone(model.voltage_override)
        self.assertNotEqual(model.voltage_v, 3.20)

    def test_sensor_aliases_and_comma_lists(self):
        selected = parse_sensor_selection(
            ["vbat,sunlight", "temp"],
            EpsModel.available_sensors,
            {"vbat": "battery", "sunlight": "solar", "temp": "temperature"},
        )
        self.assertEqual(selected, {"battery", "solar", "temperature"})


if __name__ == "__main__":
    unittest.main()

