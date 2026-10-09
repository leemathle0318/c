#!/usr/bin/env python3
"""Offline diagnostics summary. Requires Python 3; reads COUNTERS only, no audio."""
import argparse
from pathlib import Path
from datetime import datetime

FIELDS = (
    "AUStart", "AURender", "bus0", "bus1", "framesBus1", "pcmNonempty",
    "renderErrors", "AUSetProperty", "setBus1FourByte", "AUCreate",
    "RemoteIO", "VoiceProcessingIO", "otherComponents",
)

def read_records(path):
    records = []
    status = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("#"):
            status.append(line)
            continue
        if not line.startswith("epoch="):
            continue
        record = {}
        for token in line.split():
            if "=" not in token:
                continue
            k, v = token.split("=", 1)
            record[k] = int(v) if v.isdigit() else v
        if "epoch" in record and "AURender" in record:
            records.append(record)
    return records, status

def report(records, status, limit):
    print("\n".join(status))
    if len(records) < 2:
        print("\n可用快照不足：至少需要 2 行 epoch= 日志。")
        return
    print("\n每段间隔计数变化（所有数值只用于诊断，不代表音频已成功替换）")
    print("开始时间\t秒数\tbus1增量\tVoiceProcessingIO增量\tRemoteIO增量\tAudioUnitRender总增量")
    pairs = list(zip(records, records[1:]))[-limit:]
    for previous, current in pairs:
        # Ignore data across an app relaunch when counters reset.
        if current["AURender"] < previous["AURender"]:
            print("-- 检测到 TikTok 进程/计数重置，后续单独分析 --")
            continue
        timestamp = datetime.fromtimestamp(previous["epoch"]).strftime("%m-%d %H:%M:%S")
        diff = lambda key: current.get(key, 0) - previous.get(key, 0)
        print(f"{timestamp}\t{current['epoch']-previous['epoch']}\t"
              f"{diff('bus1')}\t{diff('VoiceProcessingIO')}\t"
              f"{diff('RemoteIO')}\t{diff('AURender')}")
    a, b = records[0], records[-1]
    print("\n全文件首末快照差（若跨 App 重启，整体差可能无意义）")
    for key in FIELDS:
        delta = b.get(key, 0) - a.get(key, 0)
        print(f"{key:20} {delta:+d}")
    print("\n判断原则：LIVE 时 bus1 大于零，只能证明该进程经过 AudioUnitRender 的 bus 1，")
    print("不能仅凭这些计数断言已定位 OBS 音频入口，也不能断言 VCam 成功替换了麦克风。")
    print("若全部为零，请首先检查日志中的 MSHookFunction 和 hooked 状态。")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Inspect VCam passive audio probe log")
    parser.add_argument("logfile", help="VCamLiveProbe.log path")
    parser.add_argument("--tail", type=int, default=30, help="Last N intervals")
    args = parser.parse_args()
    recs, header = read_records(args.logfile)
    report(recs, header, max(1, args.tail))
