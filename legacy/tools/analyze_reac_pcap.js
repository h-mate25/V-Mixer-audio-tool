#!/usr/bin/env node

const fs = require("fs");

const REAC_ETHERTYPE = 0x8819;
const PAYLOAD_OFFSET = 50;

function macText(buffer, offset) {
  const parts = [];
  for (let i = 0; i < 6; i += 1) {
    parts.push(buffer[offset + i].toString(16).padStart(2, "0"));
  }
  return parts.join(":");
}

function decodeSample(payload, channels, channel, frame) {
  const base = frame * channels * 3 + (channel & ~1) * 3;
  let raw;
  if (channel & 1) {
    raw = payload[base + 4] | (payload[base + 5] << 8) | (payload[base + 2] << 16);
  } else {
    raw = payload[base + 3] | (payload[base] << 8) | (payload[base + 1] << 16);
  }
  if (raw & 0x800000) {
    raw -= 0x1000000;
  }
  return raw / 8388608.0;
}

class Counter {
  constructor() {
    this.map = new Map();
  }

  add(key) {
    this.map.set(key, (this.map.get(key) || 0) + 1);
  }

  top(limit = 1000) {
    return [...this.map.entries()].sort((a, b) => b[1] - a[1]).slice(0, limit);
  }
}

class FlowStats {
  constructor() {
    this.count = 0;
    this.bytes = 0;
    this.first = null;
    this.last = null;
    this.lengths = new Counter();
    this.types = new Counter();
    this.headers = new Counter();
    this.badTrailer = 0;
    this.missing = 0;
    this.backwards = 0;
    this.duplicates = 0;
    this.maxDelta = 0;
    this.prevCounter = null;
    this.prevTime = null;
    this.gaps = [];
    this.dts = [];
    this.audioPackets = 0;
    this.nonzeroAudioPackets = 0;
    this.absSum = Array(16).fill(0);
    this.peak = Array(16).fill(0);
    this.sampleCount = 0;
  }

  add(t, packet, counter, ptype) {
    this.count += 1;
    this.bytes += packet.length;
    if (this.first === null) this.first = t;
    this.last = t;
    this.lengths.add(String(packet.length));
    this.types.add(`0x${ptype.toString(16).padStart(4, "0")}`);
    if (packet.length < 2 || packet[packet.length - 2] !== 0xc2 || packet[packet.length - 1] !== 0xea) {
      this.badTrailer += 1;
    }
    if (this.prevCounter !== null) {
      const delta = (counter - this.prevCounter) & 0xffff;
      if (delta === 0) {
        this.duplicates += 1;
      } else if (delta !== 1) {
        if (delta > 0x8000) {
          this.backwards += 1;
        } else {
          this.missing += delta - 1;
          this.maxDelta = Math.max(this.maxDelta, delta);
          if (this.gaps.length < 20) {
            this.gaps.push([this.prevCounter, counter, delta, t]);
          }
        }
      }
    }
    if (this.prevTime !== null && this.dts.length < 300000) {
      this.dts.push(t - this.prevTime);
    }
    this.prevCounter = counter;
    this.prevTime = t;
    if (packet.length >= PAYLOAD_OFFSET) {
      this.headers.add(packet.subarray(18, 50).toString("hex"));
    }

    if (ptype === 0 && packet.length === 340 && packet[packet.length - 2] === 0xc2 && packet[packet.length - 1] === 0xea) {
      const payload = packet.subarray(PAYLOAD_OFFSET, packet.length - 2);
      if (payload.length === 288) {
        this.audioPackets += 1;
        let nonzero = false;
        for (const byte of payload) {
          if (byte !== 0) {
            nonzero = true;
            break;
          }
        }
        if (nonzero) this.nonzeroAudioPackets += 1;
        for (let frame = 0; frame < 6; frame += 1) {
          for (let ch = 0; ch < 16; ch += 1) {
            const value = Math.abs(decodeSample(payload, 16, ch, frame));
            this.absSum[ch] += value;
            this.peak[ch] = Math.max(this.peak[ch], value);
          }
        }
        this.sampleCount += 6;
      }
    }
  }

  print(key) {
    const duration = this.first === null || this.last === null ? 0 : this.last - this.first;
    const rate = duration > 0 ? this.count / duration : 0;
    const sortedDts = [...this.dts].sort((a, b) => a - b);
    const percentile = (p) => sortedDts.length ? sortedDts[Math.min(sortedDts.length - 1, Math.floor(sortedDts.length * p))] : 0;
    const meanDt = sortedDts.length ? sortedDts.reduce((a, b) => a + b, 0) / sortedDts.length : 0;

    console.log(`\nflow ${key}`);
    console.log(`  count=${this.count} bytes=${this.bytes} duration=${duration.toFixed(9)}s rate=${rate.toFixed(2)}pps`);
    console.log(`  lengths={${this.lengths.top().map(([k, v]) => `${k}:${v}`).join(", ")}}`);
    console.log(`  types={${this.types.top().map(([k, v]) => `${k}:${v}`).join(", ")}}`);
    console.log(`  badTrailer=${this.badTrailer} missing=${this.missing} backwards=${this.backwards} duplicates=${this.duplicates} maxDelta=${this.maxDelta}`);
    console.log(`  dt_us mean=${(meanDt * 1e6).toFixed(3)} p50=${(percentile(0.5) * 1e6).toFixed(3)} p95=${(percentile(0.95) * 1e6).toFixed(3)} p99=${(percentile(0.99) * 1e6).toFixed(3)} max=${(percentile(1.0) * 1e6).toFixed(3)}`);
    if (this.gaps.length) {
      console.log(`  first_gaps=${this.gaps.slice(0, 8).map(([a, b, d, t]) => `${a}->${b} delta=${d} at=${t.toFixed(6)}s`).join("; ")}`);
    }
    console.log(`  top_headers=${this.headers.top(3).map(([h, c]) => `${h.slice(0, 16)}...:${c}`).join("; ")}`);
    if (this.audioPackets) {
      const peaks = this.peak.map((v, i) => [i + 1, v]).filter(([, v]) => v > 0.000001).map(([i, v]) => `${i}:${v.toFixed(4)}`).join(" ");
      const means = this.absSum
        .map((v, i) => [i + 1, this.sampleCount ? v / this.sampleCount : 0])
        .filter(([, v]) => v > 0.000001)
        .map(([i, v]) => `${i}:${v.toFixed(5)}`)
        .join(" ");
      console.log(`  audio340_16x6 packets=${this.audioPackets} nonzero=${this.nonzeroAudioPackets}`);
      console.log(`  peak_abs ${peaks || "all_zero"}`);
      console.log(`  mean_abs ${means || "all_zero"}`);
    }
  }
}

function parsePcapng(path) {
  const file = fs.readFileSync(path);
  let offset = 0;
  let littleEndian = true;
  let resolution = 1e-9;
  const readU32 = (pos) => littleEndian ? file.readUInt32LE(pos) : file.readUInt32BE(pos);
  const readU16 = (pos) => littleEndian ? file.readUInt16LE(pos) : file.readUInt16BE(pos);
  const flows = new Map();
  let total = 0;
  let reac = 0;
  let firstTime = null;
  let lastTime = null;
  let previousTime = null;
  let outOfTime = 0;

  while (offset + 12 <= file.length) {
    const blockType = file.readUInt32LE(offset);
    const blockLen = file.readUInt32LE(offset + 4);
    if (blockLen < 12 || offset + blockLen > file.length) {
      throw new Error(`invalid pcapng block at ${offset}`);
    }
    const body = offset + 8;

    if (blockType === 0x0a0d0d0a) {
      const bom = file.subarray(body, body + 4);
      if (bom.equals(Buffer.from([0x4d, 0x3c, 0x2b, 0x1a]))) {
        littleEndian = true;
      } else if (bom.equals(Buffer.from([0x1a, 0x2b, 0x3c, 0x4d]))) {
        littleEndian = false;
      } else {
        throw new Error("unknown pcapng byte order");
      }
    } else if (blockType === 0x00000001) {
      let optionOffset = body + 8;
      const end = offset + blockLen - 4;
      while (optionOffset + 4 <= end) {
        const code = readU16(optionOffset);
        const length = readU16(optionOffset + 2);
        optionOffset += 4;
        if (code === 0) break;
        if (code === 9 && length > 0) {
          const raw = file[optionOffset];
          resolution = raw & 0x80 ? 2 ** -(raw & 0x7f) : 10 ** -raw;
        }
        optionOffset += (length + 3) & ~3;
      }
    } else if (blockType === 0x00000006) {
      if (body + 20 <= offset + blockLen - 4) {
        const tsHi = readU32(body + 4);
        const tsLo = readU32(body + 8);
        const capturedLen = readU32(body + 12);
        const packetStart = body + 20;
        const packetEnd = packetStart + capturedLen;
        if (packetEnd <= offset + blockLen - 4) {
          total += 1;
          const t = (tsHi * 4294967296 + tsLo) * resolution;
          if (firstTime === null) firstTime = t;
          lastTime = t;
          if (previousTime !== null && t < previousTime) outOfTime += 1;
          previousTime = t;

          const packet = file.subarray(packetStart, packetEnd);
          if (packet.length >= 18 && packet.readUInt16BE(12) === REAC_ETHERTYPE) {
            reac += 1;
            const src = macText(packet, 6);
            const dst = macText(packet, 0);
            const counter = packet[14] | (packet[15] << 8);
            const ptype = packet[16] | (packet[17] << 8);
            const key = `${src} -> ${dst}`;
            if (!flows.has(key)) flows.set(key, new FlowStats());
            flows.get(key).add(t, packet, counter, ptype);
          }
        }
      }
    }

    offset += blockLen;
  }

  console.log(`total_packets=${total} reac_packets=${reac} duration=${(lastTime === null || firstTime === null ? 0 : lastTime - firstTime).toFixed(9)}s out_of_time=${outOfTime}`);
  [...flows.entries()].sort((a, b) => b[1].count - a[1].count).forEach(([key, stats]) => stats.print(key));
}

if (process.argv.length < 3) {
  console.error("usage: node tools/analyze_reac_pcap.js <capture.pcapng>");
  process.exit(2);
}

parsePcapng(process.argv[2]);
