#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""车辆 CAN1 总线只听监控（CANalyst-II / ControlCAN.dll，自包含脚本）。

默认：分析仪物理 CH2 接车辆 CAN1；500 kbps。
--channel 0 表示分析仪物理 CH1，--channel 1 表示物理 CH2。
所有帧写入日志，每秒显示各 ID 最新原始数据、数量、age 及电机解码。
不发送 CAN 报文。CAN ID 不能单独证明发送者；ACK 不等于电机已执行。
"""

import argparse
import ctypes
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import time

DEFAULT_DLL = Path(r"E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll")
TIMINGS = {125: (3, 0x1C), 250: (1, 0x1C), 500: (0, 0x1C), 1000: (0, 0x14)}
WIDTHS = {0x2F: 1, 0x2B: 2, 0x27: 3, 0x23: 4,
          0x4F: 1, 0x4B: 2, 0x47: 3, 0x43: 4}
OBJECTS = {0x6040: "controlword", 0x6041: "statusword", 0x6060: "mode",
           0x6061: "mode_display", 0x60FF: "left_target_rpm", 0x68FF: "right_target_rpm",
           0x6069: "left_actual_rpm", 0x6869: "right_actual_rpm",
           0x607A: "steering_target_count", 0x6064: "steering_actual_count",
           0x6080: "steering_max_rpm", 0x6081: "steering_profile_rpm",
           0x2000: "left_vendor_fault", 0x2001: "right_vendor_fault"}
SIGNED = {0x60FF, 0x68FF, 0x6069, 0x6869, 0x607A, 0x6064, 0x6060, 0x6061}


class CanObject(ctypes.Structure):
    _fields_ = [("ID", ctypes.c_uint32), ("TimeStamp", ctypes.c_uint32),
                ("TimeFlag", ctypes.c_uint8), ("SendType", ctypes.c_uint8),
                ("RemoteFlag", ctypes.c_uint8), ("ExternFlag", ctypes.c_uint8),
                ("DataLen", ctypes.c_uint8), ("Data", ctypes.c_uint8 * 8),
                ("Reserved", ctypes.c_uint8 * 3)]


class InitConfig(ctypes.Structure):
    _fields_ = [("AccCode", ctypes.c_uint32), ("AccMask", ctypes.c_uint32),
                ("Reserved", ctypes.c_uint32), ("Filter", ctypes.c_uint8),
                ("Timing0", ctypes.c_uint8), ("Timing1", ctypes.c_uint8),
                ("Mode", ctypes.c_uint8)]


class Listener:
    # 仅绑定接收所需 API；两个通道共用一次 OpenDevice，避免互相关闭 USB 设备。
    def __init__(self, dll_path, channels, bitrate=500, device_index=0, loader=None):
        self.channels, self.device_index, self.opened = channels, device_index, False
        self.bitrate = bitrate
        if loader is None:
            loader = getattr(ctypes, "WinDLL", None)
        if loader is None:
            raise RuntimeError("需要 Windows 和与 Python 位数一致的 ControlCAN.dll")
        self.dll = loader(str(dll_path))
        u32 = ctypes.c_uint32
        definitions = {"VCI_OpenDevice": [u32, u32, u32],
                       "VCI_CloseDevice": [u32, u32],
                       "VCI_InitCAN": [u32, u32, u32, ctypes.POINTER(InitConfig)],
                       "VCI_StartCAN": [u32, u32, u32],
                       "VCI_Receive": [u32, u32, u32, ctypes.POINTER(CanObject), u32, ctypes.c_int32]}
        for name, params in definitions.items():
            function = getattr(self.dll, name)
            function.argtypes, function.restype = params, u32
        self.buffer = (CanObject * 2048)()

    # 接收过滤器放行所有 ID，SDK Mode=1 为硬件只听；任一路失败即关闭本设备。
    def open(self):
        if self.dll.VCI_OpenDevice(4, self.device_index, 0) != 1:
            raise RuntimeError("打开 CANalyst-II 失败：检查 USB/驱动并关闭占用它的 CAN 软件")
        self.opened = True
        try:
            for channel in self.channels.values():
                cfg = InitConfig()
                cfg.AccMask, cfg.Filter, cfg.Mode = 0xFFFFFFFF, 1, 1
                cfg.Timing0, cfg.Timing1 = TIMINGS[self.bitrate]
                if self.dll.VCI_InitCAN(4, self.device_index, channel, ctypes.byref(cfg)) != 1:
                    raise RuntimeError("物理 CH%d 只听初始化失败" % (channel+1))
                if self.dll.VCI_StartCAN(4, self.device_index, channel) != 1:
                    raise RuntimeError("物理 CH%d 启动失败" % (channel+1))
        except BaseException:
            self.close()
            raise

    # 每路非阻塞批量读取；保留原始硬件时间戳，不假设单位或两路时钟同步。
    def receive(self, channel):
        count = int(self.dll.VCI_Receive(4, self.device_index, channel, self.buffer, len(self.buffer), 0))
        if count < 0 or count > len(self.buffer):
            raise RuntimeError("CH%d 接收失败，返回 0x%X；可能 USB 断开" % (channel+1, count))
        result = []
        for obj in self.buffer[:count]:
            result.append(dict(id=int(obj.ID), dlc=int(obj.DataLen),
                               data=bytes(obj.Data[:min(obj.DataLen, 8)]).hex(" ").upper(),
                               extended=bool(obj.ExternFlag), remote=bool(obj.RemoteFlag),
                               hardware_timestamp_raw=int(obj.TimeStamp), time_flag=int(obj.TimeFlag)))
        return result

    # 只关闭分析仪设备；不发送 NMT、不复位 ABOX 或驱动器。
    def close(self):
        if self.opened:
            self.opened = False
            self.dll.VCI_CloseDevice(4, self.device_index)


# 按当前产品源码解析标准数据帧；错误 DLC、扩展帧、远程帧只保留原始记录。
def decode(frame):
    if frame.get("extended") or frame.get("remote"):
        return None
    data = bytes.fromhex(frame["data"])
    cob, dlc = frame["id"], frame["dlc"]
    if dlc > 8 or dlc != len(data):
        return None
    if cob == 0x10A and dlc == 8:
        return dict(kind="t12_switch", a=data[2], a_enabled=data[2] != 0, b=data[3], b_action="none")
    if cob == 0x10B and dlc == 8:
        steer, throttle = struct.unpack_from("<h", data)[0], struct.unpack_from("<h", data, 4)[0]
        return dict(kind="t12_joystick", steer_raw=steer, throttle_raw=throttle,
                    reference_rpm=max(-1000, min(1000, int(throttle*1000/450))))
    if cob in (0x701, 0x702) and dlc == 1:
        return dict(kind="heartbeat", node=cob-0x700, state=data[0], bootup=data[0] == 0)
    if cob in (0x81, 0x82) and dlc == 8:
        return dict(kind="emcy", node=cob-0x80, error_code=int.from_bytes(data[:2], "little"),
                    error_register=data[2])
    if cob == 0 and dlc == 2:
        return dict(kind="nmt", command=data[0], node=data[1])
    if cob not in (0x601, 0x602, 0x581, 0x582) or dlc < 4:
        return None
    cmd, index, sub = data[0], int.from_bytes(data[1:3], "little"), data[3]
    request = cob >= 0x600
    item = dict(kind="sdo", node=cob-(0x600 if request else 0x580),
                index=index, index_hex="0x%04X" % index, subindex=sub,
                object=OBJECTS.get(index, "unknown"), command=cmd)
    if request and cmd == 0x40 and dlc == 8:
        item["action"] = "read_request"
    elif request and cmd in (0x2F, 0x2B, 0x27, 0x23) and dlc == 8:
        item["action"] = "write_request"
    elif not request and cmd == 0x60 and dlc >= 4:
        item["action"] = "write_ack"
    elif not request and cmd == 0x80 and dlc == 8:
        item.update(action="abort", abort_code="0x%08X" % int.from_bytes(data[4:8], "little"))
    elif not request and cmd in (0x4F, 0x4B, 0x47, 0x43) and dlc >= 4+WIDTHS[cmd]:
        item["action"] = "read_response"
    else:
        return None
    if item["action"] in ("write_request", "read_response"):
        width = WIDTHS[cmd]
        item["value"] = int.from_bytes(data[4:4+width], "little", signed=index in SIGNED)
    return item


class TraceState:
    # 按总线和帧类型分开统计，新鲜度使用主机单调时钟；不缓存全部历史到内存。
    def __init__(self, buses):
        self.stats, self.latest, self.pending = {}, {}, {}
        self.buses = list(buses)
        self.timeout_count, self.completed, self.aborts = 0, 0, 0
        self.max_reply_ms = 0

    # 建立观察到的 SDO 请求/应答关联；重试和未匹配应答均不会伪造已完成事务。
    def observe(self, bus, frame, elapsed):
        key = (bus, frame["id"], bool(frame.get("extended")), bool(frame.get("remote")))
        stats = self.stats.setdefault(key, dict(count=0, first_s=elapsed, last_s=elapsed, max_host_gap_ms=0))
        stats["max_host_gap_ms"] = max(stats["max_host_gap_ms"], (elapsed-stats["last_s"])*1000)
        stats["count"] += 1
        stats["last_s"] = elapsed
        stats["last_data"], stats["dlc"] = frame["data"], frame["dlc"]
        item = decode(frame)
        if not item:
            return None
        kind = item["kind"]
        label = kind
        if kind == "sdo":
            node, index, sub, action = item["node"], item["index"], item["subindex"], item["action"]
            pending_key = (bus, node, index, sub)
            if action in ("read_request", "write_request"):
                old = self.pending.get(pending_key)
                if old is None:
                    self.pending[pending_key] = dict(first_s=elapsed, last_s=elapsed, request=action, retries=0, warned=False)
                else:
                    old["last_s"], old["retries"] = elapsed, old["retries"]+1
            else:
                expected = {"write_ack": "write_request", "read_response": "read_request"}.get(action)
                pending = self.pending.get(pending_key)
                if pending and (action == "abort" or pending["request"] == expected):
                    item["observed_reply_ms"] = round((elapsed-pending["last_s"])*1000, 3)
                    self.max_reply_ms = max(self.max_reply_ms, item["observed_reply_ms"])
                    self.completed += 1
                    del self.pending[pending_key]
                if action == "abort":
                    self.aborts += 1
            label = "node%d.%s.%s:%d" % (node, action, item["object"] if item["object"] != "unknown" else item["index_hex"], sub)
        elif "node" in item:
            label += ".node%d" % item["node"]
        self.latest[bus+"."+label] = dict(at_s=elapsed, decoded=item)
        return item

    # 只报告在采集窗口内看到请求、但未看到匹配应答的情况，不能据此直接判硬件损坏。
    def expire(self, elapsed, timeout_s):
        events = []
        for (bus, node, index, sub), pending in self.pending.items():
            if not pending["warned"] and elapsed-pending["first_s"] >= timeout_s:
                pending["warned"] = True
                self.timeout_count += 1
                events.append(dict(kind="unanswered_sdo", bus=bus, node=node,
                                   index="0x%04X" % index, subindex=sub, request=pending["request"],
                                   age_ms=round((elapsed-pending["first_s"])*1000), retries=pending["retries"]))
        return events

    # 快照为每个字段单独保留 age，旧转速/目标不能被其他新报文伪装为实时反馈。
    def snapshot(self, elapsed):
        latest = {key: dict(age_ms=round((elapsed-value["at_s"])*1000, 1), **value)
                  for key, value in self.latest.items()}
        ids = {"%s.%s.0x%X" % (bus, "EXT" if ext else "STD", cob)+(".RTR" if rtr else ""):
               dict(age_ms=round((elapsed-value["last_s"])*1000, 1), **value)
               for (bus, cob, ext, rtr), value in self.stats.items()}
        return dict(kind="snapshot", elapsed_s=round(elapsed, 6), latest=latest, ids=ids,
                    unanswered_sdo_events=self.timeout_count, sdo_completed=self.completed,
                    sdo_aborts=self.aborts, pending_sdo=len(self.pending), max_observed_reply_ms=self.max_reply_ms)


# 一秒一行概要；详细解码和全量原始帧均写日志，不因终端限流而丢弃。
def console_summary(snapshot, buses):
    latest = snapshot["latest"]
    input_bus = next((bus for bus in ("CAN2", "RECEIVER") if bus in buses), None)
    fields = ("CAN2.t12_switch", "CAN2.t12_joystick",
              "CAN1.node2.write_request.left_target_rpm:0", "CAN1.node2.write_request.right_target_rpm:0",
              "CAN1.node2.read_response.left_actual_rpm:0", "CAN1.node2.read_response.right_actual_rpm:0",
              "CAN1.node1.write_request.steering_target_count:0", "CAN1.node2.read_response.statusword:0",
              "CAN1.node2.read_response.left_vendor_fault:0", "CAN1.node2.read_response.right_vendor_fault:0")
    labels = ("A", "T12", "L目标", "R目标", "L实测", "R实测", "转向目标", "行走状态", "左故障", "右故障")
    parts = []
    for bus in buses:
        records = [value for key, value in snapshot["ids"].items() if key.startswith(bus+".")]
        count = sum(value["count"] for value in records)
        age = min((value["age_ms"] for value in records), default=None)
        parts.append("%s帧数=%d/age=%s" % (bus, count, "未见" if age is None else "%dms" % age))
    for label, key in zip(labels, fields):
        if key.startswith("CAN2"):
            if input_bus is None:
                continue
            key = key.replace("CAN2.", input_bus+".")
        elif "CAN1" not in buses:
            continue
        value = latest.get(key)
        if value is None:
            parts.append(label+"=未见")
            continue
        decoded = value["decoded"]
        shown = (str(decoded["a"]) if key.endswith("switch") else
                 "%+d/%+d" % (decoded["steer_raw"], decoded["throttle_raw"]) if key.endswith("joystick") else
                 "0x%04X" % decoded["value"] if "状态" in label else str(decoded["value"]))
        parts.append("%s=%s(%dms)" % (label, shown, value["age_ms"]))
    print("[%7.1fs] %s | SDO未应答事件=%d" % (snapshot["elapsed_s"], " ".join(parts), snapshot["unanswered_sdo_events"]), flush=True)
    for key, value in snapshot["ids"].items():
        print("  %-22s DLC=%d DATA=%-23s count=%d age=%.0fms max_gap=%.1fms" % (
            key, value["dlc"], value["last_data"], value["count"], value["age_ms"], value["max_host_gap_ms"]), flush=True)


# UTF-8 JSONL 一行一条，遇到故障或 Ctrl+C 时关闭文件并生成哈希索引。
def write_json(handle, value):
    handle.write(json.dumps(value, ensure_ascii=False)+"\n")


# 分块计算原始日志哈希，长时间采集结束时无需将整个文件读入内存。
def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024*1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


# 只检测本控制台 M 键以标记故障时刻，不修改车辆状态。
def marker_pressed():
    if sys.platform != "win32" or not sys.stdin.isatty():
        return False
    import msvcrt
    marked = False
    while msvcrt.kbhit():
        key = msvcrt.getwch()
        if key in ("\x00", "\xe0"):
            msvcrt.getwch()
        marked = marked or key.lower() == "m"
    return marked


# 默认单独建立采集目录，不覆盖历史证据；实时记录不发送任何 CAN 请求。
def capture(args, listener_factory=Listener):
    label = getattr(args, "bus_label", "CAN1")
    buses = {label: args.can1_channel}
    output = args.output or Path(__file__).resolve().parent/"logs"/(label.lower()+"-"+datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    started_at = datetime.now().astimezone().isoformat()
    metadata = dict(product="patrol_vehicle_chassis", started_at=started_at, buses=buses,
                    physical_channels={bus: ch+1 for bus, ch in buses.items()}, bitrate_kbps=args.bitrate,
                    hardware_listen_only=True, transmit_api_bound=False, dll=str(args.dll),
                    python_bits=struct.calcsize("P")*8, synthetic=False,
                    script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    timing="elapsed_s=host monotonic receive batch time; hardware timestamps raw, no unit/sync assumptions",
                    limits=["CAN2 is not monitored; CAN1 alone cannot prove T12 reception by the MCU",
                            "CAN ID does not authenticate sender; isolate other masters",
                            "unchanged target may not be resent; target age is not a stop or timeout verdict",
                            "listen-only cannot measure MCU Bus-Off, internal latch or actual electrical/physical braking",
                            "capture loss/USB errors can resemble missing replies; no automatic hardware verdict"])
    if args.dll.is_file():
        metadata["dll_sha256"] = hashlib.sha256(args.dll.read_bytes()).hexdigest()
    (output/"metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    state, listener, error, reason = TraceState(buses), None, None, "duration"
    started = time.monotonic()
    print("只听采集，%d kbps；" % args.bitrate+"，".join("分析仪 CH%d -> %s" % (ch+1, bus) for bus, ch in buses.items()))
    print("输出：%s\nM 标记故障；Ctrl+C 结束。本次仅监听 %s。" % (output,label), flush=True)
    if label == "CAN1":
        print("左右转速显示驱动器原始符号（右轮镜像安装）；括号为字段 age，目标不变时固件可能不重复发送。", flush=True)
    else:
        print("接收器原始 CAN 输出；A/T12 按当前固件 0x10A/0x10B 解析，其他 ID 保留原始数据。", flush=True)
    try:
        with (output/"frames.jsonl").open("x", encoding="utf-8") as raw, (output/"events.jsonl").open("x", encoding="utf-8") as events:
            try:
                listener = listener_factory(args.dll, buses, args.bitrate, args.device_index)
                listener.open()
                last_summary = -args.interval
                while True:
                    elapsed = time.monotonic()-started
                    if getattr(args, "stop_file", None) and args.stop_file.exists():
                        reason = "operator_stop_file"
                        break
                    if args.duration and elapsed >= args.duration:
                        break
                    # 非阻塞批量读取后立即写入全量日志；终端每秒显示各 ID 最新一帧。
                    for bus, channel in buses.items():
                        batch = listener.receive(channel)
                        elapsed = time.monotonic()-started
                        for frame in batch:
                            decoded = state.observe(bus, frame, elapsed)
                            record = dict(bus=bus, adapter_channel=channel, elapsed_s=elapsed, **frame)
                            if decoded:
                                record["decoded"] = decoded
                            write_json(raw, record)
                            if args.all_frames:
                                print("[%8.3f] %s 0x%03X [%d] %s %s" % (elapsed, bus, frame["id"], frame["dlc"], frame["data"], json.dumps(decoded, ensure_ascii=False) if decoded else ""))
                    for event in state.expire(elapsed, args.reply_timeout_ms/1000):
                        write_json(events, dict(elapsed_s=elapsed, **event))
                        print("[观察告警] "+json.dumps(event, ensure_ascii=False), flush=True)
                    if marker_pressed():
                        write_json(events, dict(kind="operator_fault_marker", **{k: v for k, v in state.snapshot(elapsed).items() if k != "kind"}))
                        print("已记录故障标记", flush=True)
                    if elapsed-last_summary >= args.interval:
                        snapshot = state.snapshot(elapsed)
                        write_json(events, snapshot)
                        console_summary(snapshot, buses)
                        raw.flush()
                        events.flush()
                        last_summary = elapsed
                    time.sleep(0.002)
            except KeyboardInterrupt:
                reason = "operator_ctrl_c"
            except Exception as exc:
                error, reason = "%s: %s" % (type(exc).__name__, exc), "error"
                write_json(events, dict(kind="capture_error", elapsed_s=time.monotonic()-started, error=error))
                print("采集失败："+error, file=sys.stderr)
            finally:
                if listener is not None:
                    try:
                        listener.close()
                    except Exception as exc:
                        error, reason = "%s: %s" % (type(exc).__name__, exc), "close_error"
                        write_json(events, dict(kind="close_error", error=error))
    finally:
        summary = dict(stop_reason=reason, error=error, **state.snapshot(time.monotonic()-started))
        (output/"summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        hashes = {p.name: file_sha256(p) for p in output.iterdir() if p.is_file()}
        (output/"sha256.json").write_text(json.dumps(hashes, indent=2), encoding="utf-8")
        print("采集结束：%s；原始记录：%s" % (reason, output/"frames.jsonl"), flush=True)
    return 1 if error else 0


# 参数校验在打开 DLL/设备之前完成，只初始化所指定的一个分析仪通道。
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--channel", dest="can1_channel", type=int, choices=(0, 1), default=1, help="车辆 CAN1 所接分析仪索引；默认1=物理CH2，0=物理CH1")
    parser.add_argument("--bus-label", choices=("CAN1", "CAN2", "RECEIVER"), default="CAN1", help="记录中的实际观察位置；不改变接线或总线配置")
    parser.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    parser.add_argument("--device-index", type=int, default=0)
    parser.add_argument("--bitrate", type=int, choices=tuple(TIMINGS), default=500)
    parser.add_argument("--duration", type=float, default=0, help="秒；0=持续记录直到 Ctrl+C")
    parser.add_argument("--interval", type=float, default=1, help="概要刷新秒数")
    parser.add_argument("--reply-timeout-ms", type=float, default=250, help="被动观察 SDO 未应答告警阈值，不改变固件超时")
    parser.add_argument("--all-frames", action="store_true", help="逐帧打印；默认只限流终端，原始日志始终全量")
    parser.add_argument("--output", type=Path, help="必须是不存在的新目录，默认脚本旁 logs/can1-时间")
    parser.add_argument("--stop-file", type=Path, help="此文件出现时保存日志并关闭分析仪，便于切换到停车工具")
    args = parser.parse_args(argv)
    if any(not math.isfinite(value) for value in (args.duration, args.interval, args.reply_timeout_ms)):
        parser.error("时间参数必须为有限数值")
    if args.duration < 0 or args.interval <= 0 or args.reply_timeout_ms <= 0 or args.device_index < 0:
        parser.error("duration 和设备索引须非负，interval 和 reply-timeout-ms 须大于零")
    try:
        return capture(args)
    except OSError as exc:
        print("文件/驱动错误：%s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
