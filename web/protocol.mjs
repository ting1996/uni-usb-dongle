export const RESPONSE_FLAG = 0x80;
export const MAX_PAYLOAD_SIZE = 1024;
export const OTA_HEADER_SIZE = 512;

export const COMMAND = Object.freeze({
  begin: 1,
  data: 2,
  finish: 3,
  abort: 4,
  getDeviceState: 0x10,
  selectController: 0x11,
  removeController: 0x12,
  startDiscovery: 0x13,
  pairController: 0x14,
  getGamepadDiagnostics: 0x16,
  discoveryResult: 0x17,
  radioState: 0x18,
  pairAudio: 0x19,
  disconnectPeer: 0x1a,
  getProfileState: 0x1e,
  setProfile: 0x1f,
  getAudioMetrics: 0x20,
  getDualsenseMetrics: 0x21,
  getMicConfig: 0x22,
  setMicSource: 0x23,
  removeAudio: 0x24,
  getBlocklist: 0x25,
  blockDevice: 0x26,
  unblockDevice: 0x27,
});

export const STATUS = Object.freeze({
  ok: 0,
  invalidFrame: 1,
  invalidCommand: 2,
  busy: 3,
  updateFailed: 5,
  unsupported: 8,
  storageError: 9,
  notFound: 10,
});

export const OUTPUT_PROFILE = Object.freeze({
  generic: 0,
  xinput: 1,
  ps4: 2,
  switch: 3,
  dualsense: 4,
});

export const AUDIO_CODEC = Object.freeze({
  auto: 0,
  sbc: 1,
  aac: 2,
  ldac: 3,
});

export const MIC_SOURCE = Object.freeze({
  auto: 0,
  headphone: 1,
  controller0: 2,
  controller1: 3,
  off: 4,
});

export const MIC_CODEC = Object.freeze({
  none: 0,
  cvsd: 1,
  msbc: 2,
  opus: 3,
});

export const CAPABILITY = Object.freeze({
  bluetoothHid: 0x01,
  usbGamepad: 0x02,
  audio: 0x04,
});

const MAGIC = new Uint8Array([0x55, 0x4f, 0x54, 0x41]);

export function addressBytes(address) {
  if (!/^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(address)) {
    throw new TypeError("Choose a Bluetooth device first.");
  }
  return Uint8Array.from(address.split(":"), (byte) => Number.parseInt(byte, 16));
}

function formatAddress(bytes) {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join(":").toUpperCase();
}

export function parseDiscoveryResult(bytes) {
  if (bytes.length !== 39 || bytes[0] !== STATUS.ok) throw new TypeError("Invalid discovery result");
  const name = bytes.slice(9).filter((byte, index, all) => index < (all.indexOf(0) < 0 ? all.length : all.indexOf(0)));
  return { index: bytes[1], address: formatAddress(bytes.slice(2, 8)),
    rssi: bytes[8] > 127 ? bytes[8] - 256 : bytes[8], name: new TextDecoder().decode(name) };
}

export function parseRadioState(bytes) {
  if (![33, 44].includes(bytes.length) || bytes[0] !== STATUS.ok) throw new TypeError("Invalid radio state");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return { ready: !!bytes[1], scanning: !!bytes[2], count: bytes[3], error: bytes[4],
    connectedMask: bytes[5], streaming: !!bytes[6], headphone: formatAddress(bytes.slice(7, 13)),
    received: view.getUint32(13, true), sent: view.getUint32(17, true),
    underflows: view.getUint32(21, true), overflows: view.getUint32(25, true), errors: view.getUint32(29, true),
    audioState: bytes.length === 44 ? bytes[33] : null,
    audioErrorStage: bytes.length === 44 ? bytes[34] : 0,
    audioError: bytes.length === 44 ? bytes[35] : 0,
    audioTarget: bytes.length === 44 ? formatAddress(bytes.slice(36, 42)) : null,
    audioConnected: bytes.length === 44 ? !!bytes[42] : null,
    audioAuthenticated: bytes.length === 44 ? !!bytes[43] : null };
}

export function radioErrorMessage(code) {
  return ({0:"No error", 4:"Device did not answer the Bluetooth connection request", 5:"I/O error", 12:"Bluetooth connection resources exhausted",
    16:"Bluetooth connection is busy", 107:"Audio link disconnected",
    111:"Bluetooth connection could not start", 110:"Bluetooth operation timed out"})[code] || `Bluetooth error ${code}`;
}

export function headphoneStatus(radio) {
  if (radio.audioState !== null && radio.audioState !== undefined) {
    const states = ["Not paired or connected", "Connecting to headphone", "Authenticating headphone",
      "Paired; connecting audio profile", "Audio link connected; starting stream", "Connected and streaming", "Connection failed"];
    let label = states[radio.audioState] || "Unknown headphone state";
    const address = radio.audioTarget && radio.audioTarget !== "00:00:00:00:00:00" ? radio.audioTarget : radio.headphone;
    if (address !== "00:00:00:00:00:00") label += ` · ${address}`;
    if (radio.audioError) {
      const stage = ["", "connection", "authentication", "audio profile", "saving pairing", "disconnection"][radio.audioErrorStage] || "Bluetooth";
      label += ` · ${stage}: ${radioErrorMessage(radio.audioError)} (${radio.audioError})`;
    }
    return label;
  }
  if (radio.streaming) return `Connected and streaming · ${radio.headphone}`;
  if (radio.headphone !== "00:00:00:00:00:00") return `Pairing saved; audio stream inactive · ${radio.headphone}`;
  return `Headphone not paired${radio.error ? ` · ${radioErrorMessage(radio.error)} (${radio.error})` : ""}`;
}

export function profileLabel(profileId) {
  return ({
    [OUTPUT_PROFILE.generic]: "Generic HID",
    [OUTPUT_PROFILE.xinput]: "Xbox 360 (XInput)",
    [OUTPUT_PROFILE.ps4]: "DualShock 4 (PS4)",
    [OUTPUT_PROFILE.switch]: "Switch Pro",
    [OUTPUT_PROFILE.dualsense]: "DualSense (PS5)",
  })[profileId] || `Profile ${profileId}`;
}

export function profileVidPid(profileId) {
  return ({
    [OUTPUT_PROFILE.generic]: "FFFF:FFFF",
    [OUTPUT_PROFILE.xinput]: "045E:028E",
    [OUTPUT_PROFILE.ps4]: "054C:09CC",
    [OUTPUT_PROFILE.switch]: "057E:2009",
    [OUTPUT_PROFILE.dualsense]: "054C:0CE6",
  })[profileId] || "FFFF:FFFF";
}

export function micSourceLabel(sourceId) {
  return ({
    [MIC_SOURCE.auto]: "Auto (Headphone → Controller)",
    [MIC_SOURCE.headphone]: "Bluetooth Headphone (HFP)",
    [MIC_SOURCE.controller0]: "Controller 1 (DualSense)",
    [MIC_SOURCE.controller1]: "Controller 2 (DualSense)",
    [MIC_SOURCE.off]: "Off (Muted)",
  })[sourceId] || `Source ${sourceId}`;
}

export function micCodecLabel(codecId) {
  return ({
    [MIC_CODEC.none]: "Idle / None",
    [MIC_CODEC.cvsd]: "CVSD (8 kHz → 16 kHz)",
    [MIC_CODEC.msbc]: "mSBC (16 kHz wideband)",
    [MIC_CODEC.opus]: "Opus (DualSense 16 kHz)",
  })[codecId] || `Codec ${codecId}`;
}

export function parseProfileState(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 10 || bytes.length > 12 ||
      bytes[0] !== STATUS.ok) {
    throw new TypeError("Invalid profile state");
  }
  const savedProfile = bytes.length >= 11 ? bytes[10] : 0;
  const flags = bytes.length >= 12 ? bytes[11] : 0;
  return {
    version: bytes[1],
    active: bytes[2],
    activeLabel: profileLabel(bytes[2]),
    requested: bytes[3],
    requestedLabel: profileLabel(bytes[3]),
    profileMask: bytes[4],
    codec: bytes[5],
    streaming: bytes[6] === 0xff ? null : !!bytes[6],
    codecMask: bytes[7],
    transition: bytes[8],
    failure: bytes[9],
    savedProfile,
    savedProfileLabel: profileLabel(savedProfile),
    persisted: Boolean(flags & 0x01),
    twoControllerMode: Boolean(flags & 0x02),
  };
}

export function parseAudioMetrics(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== 12 || bytes[0] !== STATUS.ok) {
    throw new TypeError("Invalid audio metrics");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    avrcpConnected: Boolean(bytes[1] & 0x01),
    hfpSlcReady: Boolean(bytes[1] & 0x02),
    hfpScoOpen: Boolean(bytes[1] & 0x04),
    hfpMsbcActive: Boolean(bytes[1] & 0x08),
    avrcpVolume: bytes[2],
    peakLeft: view.getUint16(3, true),
    peakRight: view.getUint16(5, true),
    nonzeroPackets: view.getUint32(7, true),
    linkRoles: bytes[11],
  };
}

export function parseDualsenseMetrics(bytes) {
  if (!(bytes instanceof Uint8Array) || (bytes.length !== 30 && bytes.length < 38) ||
      bytes[0] !== STATUS.ok) {
    throw new TypeError("Invalid DualSense metrics");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const connFlags = bytes[2];
  const featMask = bytes[3];
  const base = {
    slot: bytes[1],
    connFlags,
    aclConnected: Boolean(connFlags & 0x01),
    authenticated: Boolean(connFlags & 0x02),
    controlOpen: Boolean(connFlags & 0x04),
    interruptOpen: Boolean(connFlags & 0x08),
    opening: Boolean(connFlags & 0x10),
    saved: Boolean(connFlags & 0x20),
    feat81Busy: Boolean(connFlags & 0x40),
    featMask,
    hasCal05: Boolean(featMask & 0x01),
    hasMac09: Boolean(featMask & 0x02),
    hasFw20: Boolean(featMask & 0x04),
    has08: Boolean(featMask & 0x08),
    has81: Boolean(featMask & 0x10),
    ledUnlocked: Boolean(featMask & 0x20),
    btIn31: view.getUint32(4, true),
    btIn01: view.getUint32(8, true),
    usbOut02: view.getUint32(12, true),
    btOut31Ok: view.getUint32(16, true),
    btOut31Err: view.getUint32(20, true),
    btFeatSetOk: view.getUint16(24, true),
    btFeatGetOk: view.getUint16(26, true),
    lastFeatId: bytes[28],
    lastHandshake: bytes[29],
    lastValidFlag0: null,
    lastValidFlag1: null,
    lastMotorRight: null,
    lastMotorLeft: null,
    lastRightTriggerMode: null,
    lastLeftTriggerMode: null,
    lastOutPathFlags: null,
    fwVerLo: null,
  };
  if (bytes.length >= 38) {
    base.lastValidFlag0 = bytes[30];
    base.lastValidFlag1 = bytes[31];
    base.lastMotorRight = bytes[32];
    base.lastMotorLeft = bytes[33];
    base.lastRightTriggerMode = bytes[34];
    base.lastLeftTriggerMode = bytes[35];
    base.lastOutPathFlags = bytes[36];
    base.fwVerLo = bytes[37];
  }
  return base;
}

export function parseGamepadDiagnostics(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== 27 || bytes[0] !== STATUS.ok) {
    throw new TypeError("Invalid gamepad diagnostics");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const flags = bytes[1];
  return {
    configured: Boolean(flags & 0x01),
    busy: Boolean(flags & 0x02),
    pending: Boolean(flags & 0x04),
    testActive: Boolean(flags & 0x08),
    report: Array.from(bytes.slice(2, 11)),
    submitted: view.getUint32(11, true),
    completed: view.getUint32(15, true),
    tickCount: view.getUint32(19, true),
    testStarted: view.getUint32(23, true),
  };
}

export function parseMicState(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== 16 || bytes[0] !== STATUS.ok) {
    throw new TypeError("Invalid mic state");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const flags = bytes[4];
  return {
    configuredSource: bytes[1],
    configuredSourceLabel: micSourceLabel(bytes[1]),
    activeSource: bytes[2],
    activeSourceLabel: micSourceLabel(bytes[2]),
    activeCodec: bytes[3],
    activeCodecLabel: micCodecLabel(bytes[3]),
    flags,
    usbOpen: Boolean(flags & 0x01),
    usbMuted: Boolean(flags & 0x02),
    hfpSlcReady: Boolean(flags & 0x04),
    hfpScoOpen: Boolean(flags & 0x08),
    ds0Connected: Boolean(flags & 0x10),
    ds1Connected: Boolean(flags & 0x20),
    scoCallbackSeen: Boolean(flags & 0x40),
    micPeak: view.getUint16(6, true),
    decodedFrames: view.getUint32(8, true),
    errorFrames: view.getUint32(12, true),
  };
}

export function parseBlocklist(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 3 || bytes[0] !== STATUS.ok) {
    throw new TypeError("Invalid blocklist response");
  }
  const count = bytes[1];
  const addrLen = bytes[2];
  if (addrLen !== 6 || bytes.length !== 3 + count * 6) {
    throw new TypeError("Invalid blocklist length");
  }
  const addresses = [];
  for (let i = 0; i < count; i += 1) {
    const start = 3 + i * 6;
    addresses.push(formatAddress(bytes.slice(start, start + 6)));
  }
  return addresses;
}

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function encodeFrame(command, sequence, payload = new Uint8Array()) {
  if (!Number.isInteger(command) || command < 0 || command > 0xff ||
      !Number.isInteger(sequence) || sequence < 0 || sequence > 0xffffffff ||
      !(payload instanceof Uint8Array) || payload.length > MAX_PAYLOAD_SIZE) {
    throw new TypeError("Invalid USB protocol frame");
  }

  const packet = new Uint8Array(16 + payload.length);
  packet.set(MAGIC, 0);
  packet[4] = 1;
  packet[5] = command;
  const view = new DataView(packet.buffer);
  view.setUint32(6, sequence, true);
  view.setUint16(10, payload.length, true);
  packet.set(payload, 16);
  view.setUint32(12, crc32(concat(packet.subarray(4, 12), payload)), true);
  return packet;
}

export function decodeFrame(packet) {
  if (!(packet instanceof Uint8Array) || packet.length < 16 ||
      !MAGIC.every((byte, index) => packet[index] === byte)) {
    throw new TypeError("Invalid USB protocol frame header");
  }

  const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
  const payloadSize = view.getUint16(10, true);
  if (packet[4] !== 1 || payloadSize > MAX_PAYLOAD_SIZE ||
      packet.length !== 16 + payloadSize ||
      view.getUint32(12, true) !== crc32(packet.subarray(4, 12).length
        ? concat(packet.subarray(4, 12), packet.subarray(16))
        : packet.subarray(4, 12))) {
    throw new TypeError("Invalid USB protocol frame checksum or length");
  }

  return {
    command: packet[5],
    sequence: view.getUint32(6, true),
    payload: packet.slice(16),
  };
}

function concat(left, right) {
  const bytes = new Uint8Array(left.length + right.length);
  bytes.set(left);
  bytes.set(right, left.length);
  return bytes;
}

export function parseDeviceState(payload) {
  if (!(payload instanceof Uint8Array) || payload.length !== 21 ||
      payload[0] !== STATUS.ok) {
    throw new TypeError("Invalid device-state response");
  }

  const controllers = [];
  for (let slot = 0; slot < 2; slot += 1) {
    const offset = 3 + slot * 9;
    const address = payload.slice(offset + 3, offset + 9);
    controllers.push({
      slot,
      paired: payload[offset] === 1,
      connected: payload[offset + 1] === 1,
      gamepadIndex: payload[offset + 2],
      address: [...address].map((byte) => byte.toString(16).padStart(2, "0")).join(":"),
    });
  }

  return {
    capabilities: payload[1],
    selectedSlot: payload[2],
    controllers,
  };
}

export function validateOtaImage(image) {
  if (!(image instanceof Uint8Array) || image.length < OTA_HEADER_SIZE) {
    throw new TypeError("OTA image is shorter than its 512-byte header");
  }
  const header = image.subarray(0, OTA_HEADER_SIZE);
  const signature = new TextDecoder().decode(header.subarray(0, 9));
  const compression = new TextDecoder().decode(header.subarray(16, 18));
  const bodySize = new DataView(
    header.buffer,
    header.byteOffset,
    header.byteLength,
  ).getUint32(20, true);
  if (signature !== "BL60X_OTA" || compression !== "XZ" ||
      bodySize === 0 || image.length !== OTA_HEADER_SIZE + bodySize) {
    throw new TypeError("Invalid Bouffalo XZ OTA package");
  }
  return bodySize;
}

export class SerialProtocol {
  constructor(port) {
    this.port = port;
    this.reader = null;
    this.writer = null;
    this.buffer = new Uint8Array();
    this.pending = new Map();
    this.sequence = 0;
    this.closed = false;
  }

  async open() {
    await this.port.open({ baudRate: 115200 });
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.readTask = this.readLoop();
  }

  async close() {
    this.closed = true;
    if (this.reader) {
      await this.reader.cancel();
    }
    if (this.writer) {
      this.writer.releaseLock();
    }
    if (this.reader) {
      this.reader.releaseLock();
    }
    await this.port.close();
  }

  async request(
    command,
    payload = new Uint8Array(),
    timeoutMs = 3000,
    requestedSequence = null,
  ) {
    const sequence = requestedSequence === null
      ? this.sequence
      : requestedSequence >>> 0;
    if (requestedSequence === null) {
      this.sequence = (this.sequence + 1) >>> 0;
    }
    const key = `${command | RESPONSE_FLAG}:${sequence}`;
    let timer;
    const response = new Promise((resolve, reject) => {
      this.pending.set(key, { resolve, reject });
      timer = setTimeout(() => {
        this.pending.delete(key);
        reject(new Error(`Timed out waiting for command ${command}`));
      }, timeoutMs);
    });

    try {
      await this.writer.write(encodeFrame(command, sequence, payload));
      const frame = await response;
      return frame.payload;
    } finally {
      clearTimeout(timer);
      this.pending.delete(key);
    }
  }

  async readLoop() {
    try {
      while (!this.closed) {
        const { value, done } = await this.reader.read();
        if (done) {
          break;
        }
        this.buffer = concat(this.buffer, value);
        this.dispatchFrames();
      }
    } catch (error) {
      for (const waiter of this.pending.values()) {
        waiter.reject(error);
      }
      this.pending.clear();
    }
  }

  dispatchFrames() {
    while (this.buffer.length >= 16) {
      const start = findMagic(this.buffer);
      if (start < 0) {
        this.buffer = this.buffer.slice(-3);
        return;
      }
      if (start > 0) {
        this.buffer = this.buffer.slice(start);
      }
      if (this.buffer.length < 16) {
        return;
      }
      const view = new DataView(this.buffer.buffer, this.buffer.byteOffset);
      const size = view.getUint16(10, true);
      if (size > MAX_PAYLOAD_SIZE) {
        this.buffer = this.buffer.slice(1);
        continue;
      }
      const frameSize = 16 + size;
      if (this.buffer.length < frameSize) {
        return;
      }
      try {
        const frame = decodeFrame(this.buffer.slice(0, frameSize));
        const key = `${frame.command}:${frame.sequence}`;
        const waiter = this.pending.get(key);
        if (waiter) {
          this.pending.delete(key);
          waiter.resolve(frame);
        }
        this.buffer = this.buffer.slice(frameSize);
      } catch {
        this.buffer = this.buffer.slice(1);
      }
    }
  }
}

function findMagic(bytes) {
  for (let index = 0; index <= bytes.length - MAGIC.length; index += 1) {
    if (MAGIC.every((byte, offset) => bytes[index + offset] === byte)) {
      return index;
    }
  }
  return -1;
}
