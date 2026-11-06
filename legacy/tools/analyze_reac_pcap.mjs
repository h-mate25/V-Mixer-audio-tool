import fs from "node:fs";

const REAC_ETHERTYPE = 0x8819;
const PAYLOAD_OFFSET = 50;

function macText(buffer, offset) {
  const parts = [];
  for (let index = 0; index < 6; index += 1) {
    parts.push(buffer[offset + index].toString(16).padStart(2, "0"));
  }
  return parts.join(":");
}

function decodeReacPairPacked(payload, channels, channel, frame) {
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

function decodeLinearBigEndian(payload, channels, channel, frame) {
  const base = (frame * channels + channel) * 3;
  let raw = (payload[base] << 16) | (payload[base + 1] << 8) | payload[base + 2];
  if (raw & 0x800000) {
    raw -= 0x1000000;
  }
  return raw / 8388608.0;
}

function decodeLinearLittleEndian(payload, channels, channel, frame) {
  const base = (frame * channels + channel) * 3;
  let raw = payload[base] | (payload[base + 1] << 8) | (payload[base + 2] << 16);
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
    return [...this.map.entries()].sort((left, right) => right[1] - left[1]).slice(0, limit);
  }
}

function addTopGap(list, gap, limit = 20) {
  list.push(gap);
  list.sort((left, right) => right.dt - left.dt);
  if (list.length > limit) {
    list.pop();
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
    this.previousCounter = null;
    this.previousTime = null;
    this.firstCounterGaps = [];
    this.topTimeGaps = [];
    this.dtOver1ms = 0;
    this.dtOver2ms = 0;
    this.dtOver5ms = 0;
    this.dtOver20ms = 0;
    this.audioPackets = 0;
    this.nonzeroAudioPackets = 0;
    this.peak = Array(16).fill(0);
    this.absSum = Array(16).fill(0);
    this.sampleCount = 0;
    this.firstPackets = [];
    this.lastPackets = [];
  }

  add(timestamp, packet, counter, packetType, frameNumber, relativeTime) {
    this.count += 1;
    this.bytes += packet.length;
    if (this.first === null) {
      this.first = timestamp;
    }
    this.last = timestamp;
    this.lengths.add(String(packet.length));
    this.types.add(`0x${packetType.toString(16).padStart(4, "0")}`);
    if (packet.length < 2 || packet[packet.length - 2] !== 0xc2 || packet[packet.length - 1] !== 0xea) {
      this.badTrailer += 1;
    }

    if (this.previousCounter !== null) {
      const delta = (counter - this.previousCounter) & 0xffff;
      if (delta === 0) {
        this.duplicates += 1;
      } else if (delta !== 1) {
        if (delta > 0x8000) {
          this.backwards += 1;
        } else {
          this.missing += delta - 1;
          this.maxDelta = Math.max(this.maxDelta, delta);
          if (this.firstCounterGaps.length < 16) {
            this.firstCounterGaps.push({
              previous: this.previousCounter,
              current: counter,
              delta,
              relativeTime,
              frameNumber,
            });
          }
        }
      }

      const dt = timestamp - this.previousTime;
      if (dt > 0.001) this.dtOver1ms += 1;
      if (dt > 0.002) this.dtOver2ms += 1;
      if (dt > 0.005) this.dtOver5ms += 1;
      if (dt > 0.020) this.dtOver20ms += 1;
      addTopGap(this.topTimeGaps, {
        dt,
        previous: this.previousCounter,
        current: counter,
        relativeTime,
        frameNumber,
      });
    }

    this.previousCounter = counter;
    this.previousTime = timestamp;
    if (packet.length >= PAYLOAD_OFFSET) {
      this.headers.add(packet.subarray(18, 50).toString("hex"));
    }

    const record = {
      frameNumber,
      relativeTime,
      counter,
      packetType,
      length: packet.length,
    };
    if (this.firstPackets.length < 8) {
      this.firstPackets.push(record);
    }
    this.lastPackets.push(record);
    if (this.lastPackets.length > 8) {
      this.lastPackets.shift();
    }

    if (packetType === 0 && packet.length === 340 && packet[packet.length - 2] === 0xc2 && packet[packet.length - 1] === 0xea) {
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
        if (nonzero) {
          this.nonzeroAudioPackets += 1;
        }
        for (let frame = 0; frame < 6; frame += 1) {
          for (let channel = 0; channel < 16; channel += 1) {
            const value = Math.abs(decodeReacPairPacked(payload, 16, channel, frame));
            this.absSum[channel] += value;
            this.peak[channel] = Math.max(this.peak[channel], value);
          }
        }
        this.sampleCount += 6;
      }
    }
  }
}

function createVariantStats() {
  const variants = {
    pair16x6: { decoder: decodeReacPairPacked, channels: 16, frames: 6 },
    be16x6: { decoder: decodeLinearBigEndian, channels: 16, frames: 6 },
    le16x6: { decoder: decodeLinearLittleEndian, channels: 16, frames: 6 },
    pair8x12: { decoder: decodeReacPairPacked, channels: 8, frames: 12 },
    be8x12: { decoder: decodeLinearBigEndian, channels: 8, frames: 12 },
    le8x12: { decoder: decodeLinearLittleEndian, channels: 8, frames: 12 },
  };

  const stats = {};
  for (const [name, variant] of Object.entries(variants)) {
    stats[name] = {
      ...variant,
      nonzeroPackets: 0,
      packetCount: 0,
      peak: Array(variant.channels).fill(0),
      absSum: Array(variant.channels).fill(0),
      sampleCount: 0,
    };
  }
  return stats;
}

function updateVariantStats(stats, payload) {
  for (const variant of Object.values(stats)) {
    variant.packetCount += 1;
    let nonzero = false;
    for (let frame = 0; frame < variant.frames; frame += 1) {
      for (let channel = 0; channel < variant.channels; channel += 1) {
        const value = Math.abs(variant.decoder(payload, variant.channels, channel, frame));
        if (value > 1e-7) {
          nonzero = true;
        }
        variant.absSum[channel] += value;
        variant.peak[channel] = Math.max(variant.peak[channel], value);
      }
    }
    if (nonzero) {
      variant.nonzeroPackets += 1;
    }
    variant.sampleCount += variant.frames;
  }
}

function orderedMapText(map) {
  return [...map.entries()].sort((left, right) => right[1] - left[1]).map(([key, value]) => `${key}:${value}`).join(", ");
}

function channelText(values, sampleCount = 1, threshold = 1e-6) {
  return values
    .map((value, index) => [index + 1, sampleCount ? value / sampleCount : value])
    .filter(([, value]) => value > threshold)
    .map(([channel, value]) => `${channel}:${value.toFixed(5)}`)
    .join(" ") || "all_zero";
}

function peakText(values, threshold = 1e-6) {
  return values
    .map((value, index) => [index + 1, value])
    .filter(([, value]) => value > threshold)
    .map(([channel, value]) => `${channel}:${value.toFixed(4)}`)
    .join(" ") || "all_zero";
}

function packetRecordText(records, baseTime) {
  return records.map((record) =>
    `#${record.frameNumber}@${(record.relativeTime - baseTime).toFixed(6)} c=${record.counter} pt=0x${record.packetType.toString(16)} len=${record.length}`
  ).join(" | ");
}

export function analyzePcap(path) {
  const file = fs.readFileSync(path);
  let littleEndian = true;
  let resolution = 1e-9;
  let offset = 0;
  let totalPackets = 0;
  let reacPackets = 0;
  let firstTime = null;
  let lastTime = null;
  let previousTime = null;
  let outOfTime = 0;
  let frameNumber = 0;
  const flows = new Map();
  const variants = createVariantStats();

  const readU32 = (position) => littleEndian ? file.readUInt32LE(position) : file.readUInt32BE(position);
  const readU16 = (position) => littleEndian ? file.readUInt16LE(position) : file.readUInt16BE(position);

  while (offset + 12 <= file.length) {
    const blockType = file.readUInt32LE(offset);
    const blockLength = file.readUInt32LE(offset + 4);
    if (blockLength < 12 || offset + blockLength > file.length) {
      throw new Error(`Invalid pcapng block at ${offset}`);
    }
    const body = offset + 8;
    const end = offset + blockLength - 4;

    if (blockType === 0x0a0d0d0a) {
      const bom = file.subarray(body, body + 4);
      littleEndian = !bom.equals(Buffer.from([0x1a, 0x2b, 0x3c, 0x4d]));
    } else if (blockType === 0x00000001) {
      let optionOffset = body + 8;
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
    } else if (blockType === 0x00000006 && body + 20 <= end) {
      const timestampHigh = readU32(body + 4);
      const timestampLow = readU32(body + 8);
      const capturedLength = readU32(body + 12);
      const packetStart = body + 20;
      const packetEnd = packetStart + capturedLength;
      if (packetEnd <= end) {
        frameNumber += 1;
        totalPackets += 1;
        const timestamp = (timestampHigh * 4294967296 + timestampLow) * resolution;
        if (firstTime === null) firstTime = timestamp;
        lastTime = timestamp;
        if (previousTime !== null && timestamp < previousTime) outOfTime += 1;
        previousTime = timestamp;

        const packet = file.subarray(packetStart, packetEnd);
        if (packet.length >= 18 && packet.readUInt16BE(12) === REAC_ETHERTYPE) {
          reacPackets += 1;
          const source = macText(packet, 6);
          const destination = macText(packet, 0);
          const counter = packet[14] | (packet[15] << 8);
          const packetType = packet[16] | (packet[17] << 8);
          const key = `${source} -> ${destination}`;
          if (!flows.has(key)) flows.set(key, new FlowStats());
          flows.get(key).add(timestamp, packet, counter, packetType, frameNumber, timestamp);

          if (key === "02:00:00:00:00:01 -> 02:00:00:00:00:02" &&
              packetType === 0 &&
              packet.length === 340 &&
              packet[packet.length - 2] === 0xc2 &&
              packet[packet.length - 1] === 0xea) {
            const payload = packet.subarray(PAYLOAD_OFFSET, packet.length - 2);
            if (payload.length === 288) {
              updateVariantStats(variants, payload);
            }
          }
        }
      }
    }

    offset += blockLength;
  }

  return { totalPackets, reacPackets, duration: (lastTime ?? 0) - (firstTime ?? 0), outOfTime, flows, variants };
}

export function formatAnalysis(analysis) {
  let output = `total=${analysis.totalPackets} reac=${analysis.reacPackets} duration=${analysis.duration.toFixed(9)}s out_of_time=${analysis.outOfTime}\n`;

  for (const [key, stats] of [...analysis.flows.entries()].sort((left, right) => right[1].count - left[1].count)) {
    const duration = stats.last - stats.first;
    output += `\n${key}\n`;
    output += ` count=${stats.count} bytes=${stats.bytes} dur=${duration.toFixed(9)}s rate=${(stats.count / duration).toFixed(2)}pps\n`;
    output += ` lengths={${orderedMapText(stats.lengths.map)}} types={${orderedMapText(stats.types.map)}}\n`;
    output += ` bad=${stats.badTrailer} missing=${stats.missing} back=${stats.backwards} dup=${stats.duplicates} maxDelta=${stats.maxDelta}\n`;
    output += ` dtOver 1ms=${stats.dtOver1ms} 2ms=${stats.dtOver2ms} 5ms=${stats.dtOver5ms} 20ms=${stats.dtOver20ms}\n`;
    output += ` topDt=${stats.topTimeGaps.slice(0, 10).map((gap) =>
      `${(gap.dt * 1000).toFixed(3)}ms ${gap.previous}->${gap.current} rel=${gap.relativeTime.toFixed(6)} frame=${gap.frameNumber}`
    ).join("; ")}\n`;
    if (stats.firstCounterGaps.length) {
      output += ` gaps=${stats.firstCounterGaps.map((gap) =>
        `${gap.previous}->${gap.current} d=${gap.delta} rel=${gap.relativeTime.toFixed(6)} frame=${gap.frameNumber}`
      ).join("; ")}\n`;
    }
    output += ` headers=${stats.headers.top(4).map(([header, count]) => `${header.slice(0, 16)}:${count}`).join("; ")}\n`;
    if (stats.audioPackets) {
      output += ` audio16x6=${stats.audioPackets} nonzero=${stats.nonzeroAudioPackets}\n`;
      output += ` peak=${peakText(stats.peak)}\n`;
      output += ` mean=${channelText(stats.absSum, stats.sampleCount)}\n`;
    }
    output += ` first=${packetRecordText(stats.firstPackets, 0)}\n`;
    output += ` last=${packetRecordText(stats.lastPackets, 0)}\n`;
  }

  output += "\nPC unicast decode variants:\n";
  for (const [name, stats] of Object.entries(analysis.variants)) {
    output += `${name} packets=${stats.packetCount} nonzero=${stats.nonzeroPackets} `;
    output += `peak=${peakText(stats.peak, 1e-5)} `;
    output += `mean=${channelText(stats.absSum, stats.sampleCount, 1e-5)}\n`;
  }
  return output;
}
