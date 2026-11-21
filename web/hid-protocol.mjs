import { decodeFrame, encodeFrame, RESPONSE_FLAG } from "./protocol.mjs";

export const HID_REPORT_ID = 2;
export const HID_FEATURE_SIZE = 63;
export const HID_FRAGMENT_SIZE = 60;
export const HID_FILTERS = Object.freeze([
  { vendorId: 0xffff, productId: 0xffff, usagePage: 0xff00, usage: 1 },
  { vendorId: 0x045e, productId: 0x028e, usagePage: 0xff00, usage: 1 },
  { vendorId: 0x054c, productId: 0x09cc, usagePage: 0xff00, usage: 1 },
  { vendorId: 0x057e, productId: 0x2009, usagePage: 0xff00, usage: 1 },
  { vendorId: 0x054c, productId: 0x0ce6, usagePage: 0xff00, usage: 1 },
]);

const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export function encodeHidFragments(frame) {
  const reports = [];
  for (let offset = 0; offset < frame.length; offset += HID_FRAGMENT_SIZE) {
    const data = frame.subarray(offset, offset + HID_FRAGMENT_SIZE);
    const report = new Uint8Array(HID_FEATURE_SIZE);
    new DataView(report.buffer).setUint16(0, offset, true);
    report[2] = data.length;
    report.set(data, 3);
    reports.push(report);
  }
  return reports;
}

export function decodeHidReply(view) {
  // WebHID receiveFeatureReport includes the report ID in its DataView.
  if (!(view instanceof DataView) || view.byteLength !== HID_FEATURE_SIZE + 1 ||
      view.getUint8(0) !== HID_REPORT_ID || view.getUint16(1, true) !== 0) {
    throw new Error("Invalid HID feature report");
  }
  const length = view.getUint8(3);
  if (length > HID_FRAGMENT_SIZE) {
    throw new Error("Invalid HID reply length");
  }
  return length === 0 ? null : decodeFrame(new Uint8Array(
    view.buffer, view.byteOffset + 4, length,
  ));
}

export class HidProtocol {
  constructor(device) {
    this.device = device;
    this.closed = true;
    this.sequence = 0;
    this.generation = 0;
    this.tail = Promise.resolve();
  }

  async open() {
    const supported = this.device.collections.some((collection) =>
      collection.usagePage === 0xff00 && collection.usage === 1 &&
      collection.featureReports.some((report) => report.reportId === HID_REPORT_ID));
    if (!supported) {
      throw new Error("Choose the dongle management interface; update firmware over USB serial if it is missing.");
    }
    await this.device.open();
    this.closed = false;
    this.generation += 1;
  }

  async close() {
    this.closed = true;
    this.generation += 1;
    if (this.device.opened) {
      await this.device.close();
    }
  }

  request(command, payload = new Uint8Array(), timeoutMs = 3000,
          requestedSequence = null) {
    // The firmware mailbox supports one request at a time, including OTA.
    const result = this.tail.then(() => this.exchange(
      command, payload, timeoutMs, requestedSequence,
    ));
    this.tail = result.catch(() => {});
    return result;
  }

  async exchange(command, payload, timeoutMs, requestedSequence) {
    if (this.closed || !this.device.opened) {
      throw new Error("WebHID disconnected; reconnect to the dongle.");
    }
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
      throw new TypeError("Invalid WebHID timeout");
    }
    const sequence = requestedSequence === null
      ? this.sequence++ >>> 0 : requestedSequence >>> 0;
    const frame = encodeFrame(command, sequence, payload);
    const generation = this.generation;
    const deadline = performance.now() + timeoutMs;
    const active = () => {
      if (this.closed || generation !== this.generation) {
        throw new Error("WebHID disconnected; reconnect to the dongle.");
      }
      if (performance.now() >= deadline) {
        throw new Error(`Timed out waiting for command ${command}`);
      }
    };
    const call = async (operation) => {
      active();
      let timer;
      try {
        const value = await Promise.race([
          operation(),
          new Promise((_, reject) => {
            timer = setTimeout(() => reject(new Error(
              `Timed out waiting for command ${command}`,
            )), Math.max(1, deadline - performance.now()));
          }),
        ]);
        active();
        return value;
      } finally {
        clearTimeout(timer);
      }
    };

    try {
      for (const report of encodeHidFragments(frame)) {
        await call(() => this.device.sendFeatureReport(HID_REPORT_ID, report));
      }
      for (;;) {
        const reply = decodeHidReply(await call(() =>
          this.device.receiveFeatureReport(HID_REPORT_ID)));
        if (reply && reply.command === (command | RESPONSE_FLAG) &&
            reply.sequence === sequence) {
          return reply.payload;
        }
        await delay(Math.min(10, Math.max(1, deadline - performance.now())));
        active();
      }
    } catch (error) {
      // A timed-out native call cannot be cancelled; do not let a later request
      // mix its fragments with a call that may still complete on the device.
      this.closed = true;
      this.generation += 1;
      throw error;
    }
  }
}
