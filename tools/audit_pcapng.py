"""Summarize offline Ethernet evidence; no reconstruction of damaged frames or TX.

Uses the existing narrow PCAPNG reader. Timestamp exponent must be explicitly
supplied from IDB metadata before reporting seconds/cadence; it is not guessed.
JSON contains aggregate metadata and scene hashes, never frame/PCM payloads.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from import_pcapng import records


def audit(path, timestamp_exponent=None):
    totals, lengths, types, ether, source_counts = (Counter() for _ in range(5))
    control, commands, addresses, rates, families = (Counter() for _ in range(5))
    streams, scenes, completed, orphans, transitions = {}, {}, [], Counter(), []
    first = last = None
    scale = 10 ** timestamp_exponent if timestamp_exponent is not None else None

    def elapsed(ticks):
        return (ticks - first) / scale if scale else None

    with Path(path).open("rb") as capture:
        for packet, section, interface, ticks, raw in records(capture):
            if first is None:
                first = ticks
            last = ticks
            totals["records"] += 1
            lengths[str(len(raw))] += 1
            shift = 4 if raw[12:14] == b"\x81\x00" else 0
            eth = raw[12 + shift:14 + shift].hex()
            ether[eth] += 1
            if eth != "8819":
                totals["other_or_damaged_records"] += 1
                continue
            if len(raw) < 52 + shift:
                totals["short_reac"] += 1
                continue
            totals["normally_framed_reac"] += 1
            marker_ok = raw[-2:] == b"\xc2\xea"
            totals["marker_pass" if marker_ok else "marker_fail"] += 1
            if not marker_ok:
                continue
            source = raw[6:12].hex(":")
            source_counts[source] += 1
            destination = raw[:6].hex(":")
            seq = int.from_bytes(raw[14 + shift:16 + shift], "little")
            typ = raw[16 + shift:18 + shift].hex()
            types[typ] += 1
            key = f"{section}/{interface}/{source}/{destination}/{raw[14:16].hex() if shift else 'untagged'}"
            if key not in streams:
                streams[key] = dict(count=0, first_ticks=ticks, first_seq=seq,
                                    last_ticks=ticks, last_seq=seq, previous=None,
                                    sequence_steps=Counter(), forward_step_sizes=Counter(),
                                    last_length=None)
            stream = streams[key]
            if stream["previous"] is not None:
                delta = (seq - stream["previous"]) & 65535
                category = ("consecutive" if delta == 1 else "duplicate_counter" if delta == 0
                            else "forward_gap" if delta < 32768 else "backward_or_reset")
                stream["sequence_steps"][category] += 1
                if category == "forward_gap":
                    stream["forward_step_sizes"][str(delta)] += 1
            if stream["last_length"] != len(raw):
                transitions.append(dict(packet=packet, time_seconds=elapsed(ticks), length=len(raw), source=source))
            stream.update(previous=seq, last_seq=seq, last_ticks=ticks, last_length=len(raw))
            stream["count"] += 1
            if typ == "0000":
                continue
            block = raw[18 + shift:50 + shift]
            outer_ok = sum(block) % 256 == 0
            control["outer_pass" if outer_ok else "outer_fail"] += 1
            families[block[:5].hex()] += 1
            if typ == "cfea":
                known = (block[:9] == bytes.fromhex("ffff010001030d0104")
                         and block[9:15] == raw[6:12] and 2 <= block[15] <= 40
                         and block[15] % 2 == 0 and block[16] <= 40)
                if known and outer_ok and block[17] <= 2:
                    rates[str(block[17])] += 1
                else:
                    rates["unrecognized_or_invalid"] += 1
                continue
            if typ != "cdea":
                control["unknown_frame_type"] += 1
                continue
            length = int.from_bytes(block[2:4], "big")
            if block[:2] == b"\x04\x03" and block[4:8] == bytes.fromhex("000200fe") and block[9:11] == b"\xf0\x41":
                control["sysex_candidates"] += 1
                echo = block[8]
                if 13 <= echo <= 22:
                    end = 9 + echo - 1
                    checks = dict(outer_length=length == echo + 5,
                                  terminator=block[end] == 0xf7,
                                  inner_checksum=sum(block[16:end]) % 128 == 0,
                                  seven_bit=all(x < 128 for x in block[11:end]),
                                  padding=not any(block[end + 1:31]))
                    for name, ok in checks.items():
                        control[name + ("_pass" if ok else "_fail")] += 1
                    if all(checks.values()) and outer_ok:
                        commands[f"{block[15]:02x}"] += 1
                        addresses[block[16:20].hex()] += 1
                else:
                    control["sysex_echo_out_of_bounds"] += 1
            if block[0] != 1 or block[4] != 0:
                continue
            segment = block[1] & 3
            base = 7 if segment & 1 else 5
            if not outer_ok or length > 31 - base:
                control["invalid_scene_fragment"] += 1
                if key in scenes:
                    scenes[key]["invalid_fragment_seen"] = True
                continue
            if segment & 1:
                if key in scenes:
                    unfinished = scenes.pop(key)
                    unfinished["status"] = "interrupted_by_first"
                    completed.append(unfinished)
                scenes[key] = dict(first_packet=packet, first_time=elapsed(ticks),
                                   declared=int.from_bytes(block[5:7], "big"), body=bytearray(),
                                   fragments=0, invalid_fragment_seen=False)
            if key not in scenes:
                orphans[str(segment)] += 1
                continue
            scene = scenes[key]
            scene["body"].extend(block[base:base + length])
            scene["fragments"] += 1
            if segment & 2:
                scene.update(last_packet=packet, last_time=elapsed(ticks), status="closed")
                completed.append(scenes.pop(key))
    for scene in scenes.values():
        scene["status"] = "open_at_eof"
        completed.append(scene)
    for scene in completed:
        body = scene.pop("body")
        scene.update(actual=len(body), sha256=hashlib.sha256(body).hexdigest(),
                     revision=int.from_bytes(body[20:24], "little") if len(body) >= 24 else None)
        if scene["status"] == "closed":
            scene["status"] = ("length_complete" if len(body) == scene["declared"]
                               and not scene["invalid_fragment_seen"] else "incomplete_or_invalid")
    for stream in streams.values():
        stream.pop("previous")
        stream.pop("last_length")
        dt = stream["last_ticks"] - stream["first_ticks"]
        stream["observed_packets_per_s"] = (stream["count"] - 1) * scale / dt if dt and scale else None
    return dict(records=totals, raw_lengths=lengths, ethertypes=ether, sources=source_counts,
                frame_types=types, control=control, sysex_commands=commands, sysex_addresses=addresses,
                valid_announcement_rate_classes=rates, streams=streams, length_transitions=transitions,
                scenes=completed, orphan_scene_fragments=orphans,
                timestamp_exponent=timestamp_exponent,
                duration_seconds=(last - first) / scale if first is not None and scale else None,
                limitations="Capture observations only; no physical loss, latency, device acceptance or scene commit claim.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--timestamp-exponent", type=int, choices=range(0, 13),
                        help="Explicit common decimal IDB timestamp exponent (e.g. 9 for nanoseconds)")
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.capture, args.timestamp_exponent), indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"audit_pcapng: {exc}\n")


if __name__ == "__main__":
    main()
