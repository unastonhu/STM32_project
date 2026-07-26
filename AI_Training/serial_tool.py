"""电子鼻 USB CDC 样本管理与训练数据采集工具。"""

from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
import time
from typing import Iterator


DEFAULT_BAUD_RATE = 115200
DEFAULT_TIMEOUT_SECONDS = 15.0
LABEL_NAMES = {"fresh": 0, "not_fresh": 1, "spoiled": 2}
SENSOR_STATUS_PATHS = {
    "SGP40": ("sgp", "s"),
    "ENS160/AHT21": ("env", "s"),
    "BME688": ("bme", "s"),
    "HX711": ("hx", "s"),
}


def _load_serial_modules():
    """延迟导入，使 --help 和单元测试不安装 pyserial 也能运行。"""

    try:
        import serial
        from serial.tools import list_ports
    except ImportError as exc:
        raise RuntimeError(
            "未安装 pyserial，请执行："
            "python -m pip install -r AI_Training/requirements.txt"
        ) from exc
    return serial, list_ports


def json_command(command: str, **fields: object) -> str:
    payload = {"cmd": command}
    payload.update(fields)
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=True)


def parse_json_line(line: str) -> dict[str, object] | None:
    if not line.startswith("{"):
        return None
    try:
        value = json.loads(line)
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def _nested_value(
    message: dict[str, object], path: tuple[str, ...]
) -> object | None:
    """读取 SLOW JSON 的嵌套字段；字段缺失时返回 None。"""

    value: object = message
    for key in path:
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    return value


def evaluate_preflight(
    slow: dict[str, object],
    sample_stats: dict[str, object],
    ai_status: dict[str, object],
) -> list[tuple[str, bool, str]]:
    """把板端响应转换为适合演示前查看的体检项目。"""

    checks: list[tuple[str, bool, str]] = []
    timestamp = int(slow.get("ts", 0))
    checks.append(
        (
            "设备时间",
            timestamp >= 1_577_836_800,
            f"ts={timestamp}",
        )
    )

    for name, path in SENSOR_STATUS_PATHS.items():
        status = _nested_value(slow, path)
        checks.append((name, status == 1, f"status={status}"))

    flash_cfg = ai_status.get("flash_cfg")
    flash_history = ai_status.get("flash_history")
    checks.append(
        ("配置 Flash", flash_cfg == 1, f"status={flash_cfg}")
    )
    checks.append(
        ("历史 Flash", flash_history == 1, f"status={flash_history}")
    )

    heap_min = int(ai_status.get("heap_min", 0))
    checks.append(
        (
            "FreeRTOS heap",
            heap_min > 0,
            f"历史最低剩余={heap_min} bytes",
        )
    )
    checks.append(
        (
            "样本库响应",
            sample_stats.get("cmd") == "SAMPLE_STATS",
            f"活动区间={sample_stats.get('count', '?')}",
        )
    )

    runtime_value = ai_status.get("runtime")
    runtime = runtime_value if isinstance(runtime_value, dict) else {}
    ready = runtime.get("ready")
    self_test = runtime.get("selftest")
    checks.append(
        (
            "Cube.AI Runtime",
            ready == 1 and self_test == 1,
            "ready="
            f"{ready}, selftest={self_test}, "
            f"error={runtime.get('err_type', '?')}:"
            f"{runtime.get('err_code', '?')}",
        )
    )
    checks.append(
        (
            "AI 输入输出契约",
            runtime.get("in") == 300 and runtime.get("out") == 16,
            f"{runtime.get('in', '?')} -> {runtime.get('out', '?')}",
        )
    )

    runtime_model = runtime.get("model")
    prototype_model = ai_status.get("model")
    sample_model = sample_stats.get("model")
    checks.append(
        (
            "AI 模型版本一致",
            runtime_model == prototype_model == sample_model,
            "runtime="
            f"{runtime_model}, prototype={prototype_model}, "
            f"sample={sample_model}",
        )
    )
    return checks


class ExportCapture:
    """从 FAST/SLOW 混合串口流中只截取指定样本的导出文本。"""

    def __init__(self, group_id: int) -> None:
        self.group_id = group_id
        self.started = False
        self.finished = False
        self.error: str | None = None
        self.lines: list[str] = []

    def feed(self, line: str) -> None:
        stripped = line.strip()
        if not stripped:
            return

        message = parse_json_line(stripped)
        if message is not None:
            if (
                message.get("cmd") == "EXPORT_ERROR"
                and int(message.get("id", -1)) == self.group_id
            ):
                self.error = str(message.get("reason", "unknown"))
                self.finished = True
            return

        if stripped.startswith("#EXPORT_CHUNK,"):
            metadata = _parse_metadata(stripped)
            if int(metadata.get("id", -1)) == self.group_id:
                self.started = True

        if self.started:
            self.lines.append(stripped + "\n")

        if stripped.startswith("#EXPORT_END,"):
            metadata = _parse_metadata(stripped)
            if int(metadata.get("id", -1)) == self.group_id:
                self.finished = True


def _parse_metadata(line: str) -> dict[str, str]:
    metadata: dict[str, str] = {}
    for item in line.split(",")[1:]:
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        metadata[key.strip()] = value.strip()
    return metadata


class EnoseSerialClient:
    def __init__(
        self,
        port: str,
        baud_rate: int = DEFAULT_BAUD_RATE,
    ) -> None:
        serial, _ = _load_serial_modules()
        self.connection = serial.Serial(
            port=port,
            baudrate=baud_rate,
            timeout=0.2,
            write_timeout=2.0,
        )
        # 打开 CDC 后给设备和 Windows 驱动一点稳定时间。
        time.sleep(0.4)
        self.connection.reset_input_buffer()

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> "EnoseSerialClient":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    def send_line(self, text: str) -> None:
        payload = (text.rstrip("\r\n") + "\r\n").encode("utf-8")
        self.connection.write(payload)
        self.connection.flush()

    def lines(self, timeout_seconds: float) -> Iterator[str]:
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            raw = self.connection.readline()
            if not raw:
                continue
            yield raw.decode("utf-8", errors="replace").strip()

    def request_json(
        self,
        command: str,
        expected_response: str,
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
        **fields: object,
    ) -> dict[str, object]:
        self.send_line(json_command(command, **fields))
        for line in self.lines(timeout_seconds):
            message = parse_json_line(line)
            if message is not None and message.get("cmd") == expected_response:
                return message
        raise TimeoutError(
            f"{timeout_seconds:g} 秒内没有收到 {expected_response}"
        )

    def sync_time(self, timezone_hours: int) -> None:
        utc_timestamp = int(time.time())
        self.send_line(f"TIME:{utc_timestamp},{timezone_hours}")

    def start_stream(self) -> None:
        """解除固件 WAITING 状态；不会改变 FAST/SLOW 的原有周期。"""

        self.send_line(json_command("START"))

    def wait_for_json(
        self,
        expected_response: str,
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    ) -> dict[str, object]:
        for line in self.lines(timeout_seconds):
            message = parse_json_line(line)
            if message is not None and message.get("cmd") == expected_response:
                return message
        raise TimeoutError(
            f"{timeout_seconds:g} 秒内没有收到 {expected_response}"
        )

    def export_sample(
        self,
        group_id: int,
        output_path: Path,
        timeout_seconds: float,
    ) -> int:
        self.connection.reset_input_buffer()
        self.send_line(json_command("EXPORT_SAMPLE", id=group_id))

        capture = ExportCapture(group_id)
        acknowledged = False
        for line in self.lines(timeout_seconds):
            message = parse_json_line(line)
            if message is not None and message.get("cmd") == "EXPORT_ACK":
                if int(message.get("id", -1)) != group_id:
                    continue
                if int(message.get("ok", 0)) != 1:
                    raise RuntimeError("设备拒绝导出：任务忙或样本不存在")
                acknowledged = True
                continue

            capture.feed(line)
            if capture.finished:
                break

        if not acknowledged:
            raise TimeoutError("没有收到 EXPORT_ACK")
        if capture.error is not None:
            raise RuntimeError(f"设备导出失败：{capture.error}")
        if not capture.finished:
            raise TimeoutError("导出未在超时时间内完成")

        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text("".join(capture.lines), encoding="utf-8")

        end_metadata = _parse_metadata(capture.lines[-1].strip())
        return int(end_metadata.get("records", 0))


def resolve_port(requested_port: str | None) -> str:
    _, list_ports = _load_serial_modules()
    if requested_port:
        return requested_port

    ports = list(list_ports.comports())
    if len(ports) == 1:
        return str(ports[0].device)
    if not ports:
        raise RuntimeError("没有检测到串口，请检查 USB CDC 驱动和数据线")

    choices = ", ".join(str(port.device) for port in ports)
    raise RuntimeError(f"检测到多个串口，请用 --port 指定：{choices}")


def label_value(text: str) -> int:
    lowered = text.lower()
    if lowered in LABEL_NAMES:
        return LABEL_NAMES[lowered]
    try:
        value = int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "label 使用 fresh/not_fresh/spoiled 或 0/1/2"
        ) from exc
    if value not in (0, 1, 2):
        raise argparse.ArgumentTypeError("label 必须是 0、1 或 2")
    return value


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="STM32 电子鼻样本管理和训练数据导出"
    )
    parser.add_argument("--port", help="例如 COM5；只有一个串口时可省略")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD_RATE)
    subparsers = parser.add_subparsers(dest="action", required=True)

    subparsers.add_parser("ports", help="列出串口")

    sync = subparsers.add_parser("sync-time", help="同步 UTC 和时区")
    sync.add_argument("--tz", type=int, default=8)

    monitor = subparsers.add_parser("monitor", help="持续显示并保存串口流")
    monitor.add_argument("--output", type=Path)

    preflight = subparsers.add_parser(
        "preflight", help="采样前检查时间、传感器、Flash 和任务状态"
    )
    preflight.add_argument("--tz", type=int, default=8)
    preflight.add_argument("--timeout", type=float, default=20.0)

    add = subparsers.add_parser("add", help="新增时间戳样本")
    add.add_argument("--start", type=int, required=True)
    add.add_argument("--end", type=int, required=True)
    add.add_argument("--label", type=label_value, required=True)

    update = subparsers.add_parser("update", help="修改样本")
    update.add_argument("--id", type=int, required=True)
    update.add_argument("--start", type=int, required=True)
    update.add_argument("--end", type=int, required=True)
    update.add_argument("--label", type=label_value, required=True)

    delete = subparsers.add_parser("delete", help="删除样本")
    delete.add_argument("--id", type=int, required=True)

    get_sample = subparsers.add_parser("get", help="按活动序号查询样本")
    get_sample.add_argument("--index", type=int, required=True)

    subparsers.add_parser("stats", help="查询样本库")
    subparsers.add_parser("ai-status", help="查询原型和 FreeRTOS heap")
    subparsers.add_parser("rebuild", help="手动请求原型重建")

    export = subparsers.add_parser("export", help="按样本 ID 导出 CSV")
    export.add_argument("--id", type=int, required=True)
    export.add_argument("--output", type=Path)
    export.add_argument("--timeout", type=float, default=180.0)
    return parser


def print_ports() -> int:
    _, list_ports = _load_serial_modules()
    ports = list(list_ports.comports())
    if not ports:
        print("没有检测到串口")
        return 1
    for port in ports:
        print(f"{port.device}\t{port.description}\t{port.hwid}")
    return 0


def default_export_path(group_id: int) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return Path("AI_Training/data") / f"sample_{group_id}_{stamp}.txt"


def main() -> int:
    args = create_parser().parse_args()
    if args.action == "ports":
        return print_ports()

    try:
        port = resolve_port(args.port)
        with EnoseSerialClient(port, args.baud) as client:
            if args.action == "sync-time":
                client.sync_time(args.tz)
                print(f"已发送时间同步：UTC={int(time.time())}, TZ={args.tz}")
                return 0

            if args.action == "monitor":
                # monitor 面向人工采样，自动 START 避免设备一直停在 WAITING。
                client.start_stream()
                output = (
                    args.output.open("a", encoding="utf-8")
                    if args.output
                    else None
                )
                try:
                    print("开始监听，按 Ctrl+C 结束")
                    while True:
                        for line in client.lines(3600.0):
                            print(line)
                            if output is not None:
                                output.write(line + "\n")
                                output.flush()
                except KeyboardInterrupt:
                    return 0
                finally:
                    if output is not None:
                        output.close()

            if args.action == "preflight":
                # 先同步时间并取得一帧真实 SLOW，再查询两个管理模块。
                client.start_stream()
                # 固件 CDC 只有一个待处理接收槽；等待上一条命令被任务取走。
                # START 未被取走前紧接着发送 TIME 会让第二包被主动丢弃。
                time.sleep(1.5)
                client.sync_time(args.tz)
                slow = client.wait_for_json("SLOW", args.timeout)
                sample_stats = client.request_json(
                    "SAMPLE_STATS", "SAMPLE_STATS"
                )
                ai_status = client.request_json("AI_STATUS", "AI_STATUS")
                checks = evaluate_preflight(
                    slow, sample_stats, ai_status
                )
                for name, passed, detail in checks:
                    print(f"[{'通过' if passed else '失败'}] {name}: {detail}")
                print(
                    "模型状态："
                    f"version={ai_status.get('model', '?')}, "
                    f"generation={ai_status.get('generation', '?')}, "
                    f"labels={ai_status.get('labels', '?')}"
                )
                runtime_value = ai_status.get("runtime")
                runtime = (
                    runtime_value
                    if isinstance(runtime_value, dict)
                    else {}
                )
                print(
                    "Cube.AI 计数："
                    f"success={runtime.get('ok', '?')}, "
                    f"failed={runtime.get('fail', '?')}, "
                    f"rejected={runtime.get('rejected', '?')}, "
                    f"mutex_timeout={runtime.get('mutex_to', '?')}"
                )
                if runtime.get("smoke") == 1:
                    print(
                        "[提示] 当前仍是随机权重冒烟模型，"
                        "只能验证数据流，不能代表新鲜度准确率。"
                    )
                worker_value = ai_status.get("worker")
                worker = (
                    worker_value
                    if isinstance(worker_value, dict)
                    else {}
                )
                print(
                    "低优先级 AI Worker："
                    f"推理成功={worker.get('infer_ok', '?')}, "
                    f"正常跳过={worker.get('infer_skip', '?')}, "
                    f"重建={worker.get('rebuild_ok', '?')}/"
                    f"{worker.get('rebuild_req', '?')}, "
                    f"导出={worker.get('export_ok', '?')}/"
                    f"{worker.get('export_req', '?')}"
                )
                return 0 if all(item[1] for item in checks) else 3

            if args.action == "add":
                response = client.request_json(
                    "SAMPLE_ADD",
                    "SAMPLE_ACK",
                    start=args.start,
                    end=args.end,
                    label=args.label,
                )
            elif args.action == "update":
                response = client.request_json(
                    "SAMPLE_UPDATE",
                    "SAMPLE_ACK",
                    id=args.id,
                    start=args.start,
                    end=args.end,
                    label=args.label,
                )
            elif args.action == "delete":
                response = client.request_json(
                    "SAMPLE_DELETE", "SAMPLE_ACK", id=args.id
                )
            elif args.action == "get":
                response = client.request_json(
                    "SAMPLE_GET", "SAMPLE_DATA", index=args.index
                )
            elif args.action == "stats":
                response = client.request_json(
                    "SAMPLE_STATS", "SAMPLE_STATS"
                )
            elif args.action == "ai-status":
                response = client.request_json("AI_STATUS", "AI_STATUS")
            elif args.action == "rebuild":
                response = client.request_json(
                    "AI_REBUILD", "AI_REBUILD_ACK"
                )
            elif args.action == "export":
                output_path = args.output or default_export_path(args.id)
                records = client.export_sample(
                    args.id, output_path, args.timeout
                )
                print(f"导出完成：{records} 条 -> {output_path}")
                return 0
            else:
                raise RuntimeError(f"未知操作：{args.action}")

            print(json.dumps(response, ensure_ascii=False, indent=2))
            return 0
    except (RuntimeError, TimeoutError, OSError) as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
