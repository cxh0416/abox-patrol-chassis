"""CAN1 只听脚本主机回归；严禁访问实际 DLL 或 USB-CAN。"""
from contextlib import redirect_stdout, redirect_stderr
import ctypes
import hashlib
from io import StringIO
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import can1_bus_monitor as monitor


def frame(cob=0x602, data='23 FF 60 00 18 FC FF FF', **extras):
    return dict(id=cob, data=data, dlc=len(bytes.fromhex(data)), extended=False, remote=False, **extras)


class Function:
    def __init__(self, callback):
        self.callback = callback
        self.calls = []

    def __call__(self, *args):
        self.calls.append(args)
        return self.callback(*args)


class FakeDLL:
    def __init__(self, start=1, receive=-1):
        self.configs = []
        self.VCI_OpenDevice = Function(lambda *args: 1)
        self.VCI_CloseDevice = Function(lambda *args: 1)
        self.VCI_InitCAN = Function(self.initialize)
        self.VCI_StartCAN = Function(lambda *args: start)
        self.VCI_Receive = Function(lambda *args: receive)

    def initialize(self, device, index, channel, config):
        cfg = ctypes.cast(config, ctypes.POINTER(monitor.InitConfig)).contents
        self.configs.append((channel, cfg.Mode, cfg.Timing0, cfg.Timing1, cfg.AccMask))
        return 1

    def __getattr__(self, name):
        if 'Transmit' in name or 'Send' in name:
            raise AssertionError('尝试绑定发送接口')
        raise AttributeError(name)


class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.guard = patch.object(ctypes, 'WinDLL', create=True, side_effect=AssertionError('禁止实际 DLL'))
        self.guard.start()
        self.addCleanup(self.guard.stop)

    def test_sdk_abi_and_listen_only_channel(self):
        self.assertEqual(ctypes.sizeof(monitor.CanObject), 24)
        self.assertEqual(ctypes.sizeof(monitor.InitConfig), 16)
        self.assertEqual(monitor.CanObject.Data.offset, 13)
        dll = FakeDLL()
        listener = monitor.Listener('fake', {'CAN1': 1}, loader=lambda _: dll)
        listener.open()
        listener.close()
        listener.close()
        self.assertEqual(dll.configs, [(1, 1, 0, 0x1C, 0xFFFFFFFF)])
        self.assertEqual(len(dll.VCI_OpenDevice.calls), 1)
        self.assertEqual(len(dll.VCI_CloseDevice.calls), 1)

    def test_partial_open_failure_closes_device(self):
        dll = FakeDLL(start=0)
        listener = monitor.Listener('fake', {'CAN1': 0}, loader=lambda _: dll)
        with self.assertRaises(RuntimeError):
            listener.open()
        self.assertEqual(len(dll.VCI_CloseDevice.calls), 1)

    def test_dual_channels_are_listen_only_and_share_device_lifetime(self):
        dll = FakeDLL()
        listener = monitor.Listener('fake', {'CAN1': 1, 'CAN2': 0}, loader=lambda _: dll)
        listener.open()
        listener.close()
        self.assertEqual(dll.configs, [(1, 1, 0, 0x1C, 0xFFFFFFFF), (0, 1, 0, 0x1C, 0xFFFFFFFF)])
        self.assertEqual(len(dll.VCI_OpenDevice.calls), 1)
        self.assertEqual(len(dll.VCI_CloseDevice.calls), 1)
        dll = FakeDLL()
        dll.VCI_StartCAN = Function(lambda device, index, channel: 0 if channel == 0 else 1)
        listener = monitor.Listener('fake', {'CAN1': 1, 'CAN2': 0}, loader=lambda _: dll)
        with self.assertRaises(RuntimeError):
            listener.open()
        self.assertEqual(len(dll.VCI_CloseDevice.calls), 1)

    def test_dual_capture_preserves_input_and_output_bus_identity(self):
        class FakeListener:
            def __init__(self, dll, buses, bitrate, device_index):
                self.calls = 0
                self.buses = buses
            def open(self):
                pass
            def receive(self, channel):
                self.calls += 1
                if self.calls > 2:
                    raise KeyboardInterrupt
                return [frame(0x10A, '00 00 02 00 00 00 00 00')] if channel == 0 else [frame()]
            def close(self):
                pass
        with TemporaryDirectory() as tmp:
            args = SimpleNamespace(can1_channel=1, can2_channel=0, output=Path(tmp)/'dual',
                                   dll=Path('fake.dll'), bitrate=500, device_index=0, duration=0,
                                   interval=1, reply_timeout_ms=200, all_frames=False)
            with redirect_stdout(StringIO()), patch.object(monitor, 'marker_pressed', return_value=False):
                self.assertEqual(monitor.capture(args, FakeListener), 0)
            rows = [json.loads(line) for line in (args.output/'frames.jsonl').read_text().splitlines()]
            self.assertEqual([(r['bus'], r['adapter_channel']) for r in rows], [('CAN1', 1), ('CAN2', 0)])
            metadata = json.loads((args.output/'metadata.json').read_text(encoding='utf-8'))
            self.assertEqual(metadata['physical_channels'], {'CAN1': 2, 'CAN2': 1})
            self.assertTrue(metadata['hardware_listen_only'])
            self.assertFalse(metadata['transmit_api_bound'])

    def test_usb_receive_error_is_not_empty_traffic(self):
        dll = FakeDLL(receive=0xFFFFFFFF)
        listener = monitor.Listener('fake', {'CAN1': 0}, loader=lambda _: dll)
        listener.open()
        with self.assertRaises(RuntimeError):
            listener.receive(0)
        listener.close()

    def test_signed_targets_and_compact_driver_responses(self):
        decoded = monitor.decode(frame())
        self.assertEqual((decoded['node'], decoded['object'], decoded['value']), (2, 'left_target_rpm', -1000))
        reply = monitor.decode(frame(0x582, '4B 41 60 00 37 00'))
        self.assertEqual((reply['action'], reply['value']), ('read_response', 0x37))
        ack = monitor.decode(frame(0x582, '60 FF 60 00'))
        self.assertEqual(ack['action'], 'write_ack')

    def test_unknown_extended_remote_and_truncated_frames_preserved_not_decoded(self):
        for item in (frame(0x123, '01 02'), dict(frame(), extended=True),
                     dict(frame(), remote=True), frame(0x582, '43 69 60 00 01 02')):
            self.assertIsNone(monitor.decode(item))
        state = monitor.TraceState({'CAN1': 1})
        state.observe('CAN1', frame(0x123, '01 02'), 0)
        snap = state.snapshot(1)
        self.assertEqual(snap['ids']['CAN1.STD.0x123']['count'], 1)
        self.assertEqual(snap['ids']['CAN1.STD.0x123']['last_data'], '01 02')

    def test_reply_matching_timeout_and_independent_age(self):
        state = monitor.TraceState({'CAN1': 1})
        state.observe('CAN1', frame(), 0)
        state.observe('CAN1', frame(0x582, '60 FF 68 00'), .01)
        self.assertEqual(len(state.pending), 1)  # wrong object cannot acknowledge a request
        self.assertEqual(len(state.expire(.25, .25)), 1)
        self.assertEqual(state.expire(.5, .25), [])
        state.observe('CAN1', frame(0x582, '60 FF 60 00'), .6)
        self.assertEqual(len(state.pending), 0)
        self.assertEqual(state.completed, 1)
        self.assertEqual(state.snapshot(1)['latest']['CAN1.node2.write_request.left_target_rpm:0']['age_ms'], 1000)

    def test_receive_batch_preserves_hardware_fields(self):
        dll = FakeDLL()
        def receive(device, index, channel, buffer, capacity, timeout):
            obj = buffer[0]
            obj.ID, obj.TimeStamp, obj.TimeFlag, obj.DataLen, obj.ExternFlag = 0x123456, 42, 1, 2, 1
            obj.Data[0], obj.Data[1] = 0xAB, 0xCD
            self.assertEqual(timeout, 0)
            return 1
        dll.VCI_Receive = Function(receive)
        listener = monitor.Listener('fake', {'CAN1': 0}, loader=lambda _: dll)
        listener.open()
        item = listener.receive(0)[0]
        listener.close()
        self.assertEqual((item['data'], item['hardware_timestamp_raw'], item['time_flag'], item['extended']), ('AB CD', 42, 1, True))

    def test_capture_flushes_all_raw_frames_and_creates_hashes(self):
        class FakeListener:
            def __init__(self, dll, buses, bitrate, device_index):
                self.closed, self.called = False, False
                self.buses = buses
            def open(self):
                pass
            def receive(self, channel):
                if self.called:
                    raise KeyboardInterrupt
                self.called = True
                return [frame(), frame(0x582, '60 FF 60 00'), frame(0x123, '01 02')]
            def close(self):
                self.closed = True
        with TemporaryDirectory() as tmp:
            output = Path(tmp)/'capture'
            args = SimpleNamespace(can1_channel=1, output=output, dll=Path('fake.dll'), bitrate=500,
                                   device_index=0, duration=0, interval=1, reply_timeout_ms=250, all_frames=False)
            created = []
            def factory(*params):
                listener = FakeListener(*params)
                created.append(listener)
                return listener
            with redirect_stdout(StringIO()), patch.object(monitor, 'marker_pressed', return_value=False):
                self.assertEqual(monitor.capture(args, factory), 0)
            rows = [json.loads(row) for row in (output/'frames.jsonl').read_text(encoding='utf-8').splitlines()]
            self.assertEqual(len(rows), 3)
            self.assertTrue(created[0].closed)
            self.assertEqual(created[0].buses, {'CAN1': 1})
            self.assertEqual(json.loads((output/'summary.json').read_text())['stop_reason'], 'operator_ctrl_c')
            for name, digest in json.loads((output/'sha256.json').read_text()).items():
                self.assertEqual(hashlib.sha256((output/name).read_bytes()).hexdigest(), digest)
            with self.assertRaises(FileExistsError):
                monitor.capture(args, factory)

    def test_failure_still_saves_summary(self):
        with TemporaryDirectory() as tmp:
            args = SimpleNamespace(can1_channel=0, output=Path(tmp)/'capture', dll=Path('fake'), bitrate=500,
                                   device_index=0, duration=0, interval=1, reply_timeout_ms=250, all_frames=False)
            with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
                self.assertEqual(monitor.capture(args, lambda *args: (_ for _ in ()).throw(OSError('USB missing'))), 1)
            summary = json.loads((args.output/'summary.json').read_text())
            self.assertEqual(summary['stop_reason'], 'error')
            self.assertIn('USB missing', summary['error'])

    def test_cli_checks_before_opening_device(self):
        for argv in (['--duration', 'nan'], ['--interval', '0'], ['--duration', '-1'], ['--channel', '2'],
                     ['--channel', '1', '--can2-channel', '1'],
                     ['--bus-label', 'RECEIVER', '--channel', '1', '--can2-channel', '0']):
            with redirect_stderr(StringIO()), self.assertRaises(SystemExit) as stopped:
                monitor.main(argv)
            self.assertEqual(stopped.exception.code, 2)
        with patch.object(monitor, 'capture', return_value=0) as capture:
            self.assertEqual(monitor.main(['--channel', '0', '--duration', '1']), 0)
            self.assertEqual(capture.call_args.args[0].can1_channel, 0)

    def test_receiver_label_displays_input_and_keeps_actual_bus_name(self):
        state = monitor.TraceState({'RECEIVER': 1})
        state.observe('RECEIVER', frame(0x10A, '01 01 02 00 00 01 00 00'), 0)
        state.observe('RECEIVER', frame(0x10B, 'C2 01 00 00 3E FE 00 00'), .04)
        output = StringIO()
        with redirect_stdout(output):
            monitor.console_summary(state.snapshot(.1), {'RECEIVER': 1})
        self.assertIn('A=2', output.getvalue())
        self.assertIn('T12=+450/-450', output.getvalue())
        self.assertNotIn('L目标', output.getvalue())
        with patch.object(monitor, 'capture', return_value=0) as capture:
            monitor.main(['--channel', '1', '--bus-label', 'RECEIVER'])
            self.assertEqual(capture.call_args.args[0].bus_label, 'RECEIVER')


if __name__ == '__main__':
    unittest.main()
