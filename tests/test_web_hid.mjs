import test from "node:test";
import assert from "node:assert/strict";
import { HidProtocol, HID_FILTERS, encodeHidFragments, decodeHidReply } from "../web/hid-protocol.mjs";
import { encodeFrame, decodeFrame, COMMAND } from "../web/protocol.mjs";

function feature(frame = new Uint8Array()) {
  const data = new Uint8Array(64);
  data[0] = 2;
  data[3] = frame.length;
  data.set(frame, 4);
  return new DataView(data.buffer);
}

class Device {
  opened = false;
  collections = [{ usagePage: 0xff00, usage: 1, featureReports: [{ reportId: 2 }] }];
  received = [];
  requests = [];
  response = feature();
  polls = 0;
  async open() { this.opened = true; }
  async close() { this.opened = false; }
  async sendFeatureReport(id, data) {
    assert.equal(id, 2);
    assert.equal(data.length, 63);
    const offset = new DataView(data.buffer, data.byteOffset).getUint16(0, true);
    assert.equal(offset, this.received.length);
    this.received.push(...data.slice(3, 3 + data[2]));
    if (this.received.length >= 16) {
      const bytes = Uint8Array.from(this.received);
      const length = 16 + new DataView(bytes.buffer).getUint16(10, true);
      if (bytes.length === length) {
        const frame = decodeFrame(bytes);
        this.requests.push(frame);
        this.response = feature(encodeFrame(frame.command | 0x80,
          frame.sequence, Uint8Array.of(0, 0, 2, 0, 0)));
        this.received = [];
      }
    }
  }
  async receiveFeatureReport(id) {
    assert.equal(id, 2);
    this.polls += 1;
    return this.polls === 1 ? feature() : this.response;
  }
}

test("large OTA requests retain every byte across HID offsets", () => {
  const payload = Uint8Array.from({ length: 1024 }, (_, i) => i & 255);
  const packet = encodeFrame(COMMAND.data, 512, payload);
  const fragments = encodeHidFragments(packet);
  assert.equal(fragments.length, 18);
  const reassembled = [];
  for (const fragment of fragments) {
    assert.equal(new DataView(fragment.buffer).getUint16(0, true), reassembled.length);
    reassembled.push(...fragment.slice(3, 3 + fragment[2]));
  }
  assert.deepEqual(Uint8Array.from(reassembled), packet);
});

test("feature replies validate report ID, offset, count and frame checksum", () => {
  const frame = encodeFrame(0x90, 7, Uint8Array.of(0, 2));
  assert.deepEqual(decodeHidReply(feature(frame)), decodeFrame(frame));
  assert.equal(decodeHidReply(feature()), null);
  for (const [index, value] of [[0, 3], [1, 1], [3, 61], [15, 99]]) {
    const bad = feature(frame);
    bad.setUint8(index, value);
    assert.throws(() => decodeHidReply(bad));
  }
});

test("WebHID sends a fragmented BEGIN and serializes queued requests", async () => {
  const device = new Device();
  const protocol = new HidProtocol(device);
  await protocol.open();
  const payload = new Uint8Array(516).fill(0xa5);
  await Promise.all([
    protocol.request(COMMAND.begin, payload, 1000, 0),
    protocol.request(COMMAND.getDeviceState),
  ]);
  assert.equal(device.requests.length, 2);
  assert.deepEqual(device.requests[0].payload, payload);
  assert.equal(device.requests[1].command, COMMAND.getDeviceState);
  assert.ok(device.polls >= 3);
  await protocol.close();
});

test("timed out native calls cannot interleave with another request", async () => {
  const device = new Device();
  device.sendFeatureReport = () => new Promise(() => {});
  const protocol = new HidProtocol(device);
  await protocol.open();
  await assert.rejects(protocol.request(COMMAND.getDeviceState, undefined, 15), /Timed out/);
  await assert.rejects(protocol.request(COMMAND.getDeviceState), /disconnected/);
  await protocol.close();
});

test("closing rejects in-flight work and queued requests", async () => {
  const device = new Device();
  device.receiveFeatureReport = async () => feature();
  const protocol = new HidProtocol(device);
  await protocol.open();
  const work = protocol.request(COMMAND.getDeviceState);
  await new Promise((resolve) => setTimeout(resolve, 1));
  await protocol.close();
  await assert.rejects(work, /disconnected/);
});

test("wrong management interface fails before device open", async () => {
  const device = new Device();
  device.collections = [];
  await assert.rejects(new HidProtocol(device).open(), /management interface/);
  assert.equal(device.opened, false);
});

test("HID_FILTERS includes management interfaces for all five output profiles", () => {
  const keys = HID_FILTERS.map((f) => `${f.vendorId.toString(16)}:${f.productId.toString(16)}`);
  assert.deepEqual(keys, ["ffff:ffff", "45e:28e", "54c:9cc", "57e:2009", "54c:ce6"]);
  for (const filter of HID_FILTERS) {
    assert.equal(filter.usagePage, 0xff00);
    assert.equal(filter.usage, 1);
  }
});
