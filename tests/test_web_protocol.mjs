import test from "node:test";
import assert from "node:assert/strict";

import {
  COMMAND,
  STATUS,
  decodeFrame,
  encodeFrame,
  parseDeviceState,
  validateOtaImage,
} from "../web/protocol.mjs";

test("frame encoding matches the host and firmware wire format", () => {
  const payload = new Uint8Array([0x10, 0x20, 0x30, 0x40]);
  const frame = encodeFrame(COMMAND.data, 0x12345678, payload);
  assert.equal(
    [...frame].map((byte) => byte.toString(16).padStart(2, "0")).join(""),
    "554f544101027856341204003b3a53f710203040",
  );
  assert.deepEqual(decodeFrame(frame), {
    command: COMMAND.data,
    sequence: 0x12345678,
    payload,
  });
});

test("device state parsing keeps slots and output indexes independent", () => {
  const payload = new Uint8Array(21);
  payload[0] = STATUS.ok;
  payload[1] = 0;
  payload[2] = 1;
  payload.set([1, 0, 0, 1, 2, 3, 4, 5, 6], 3);
  payload.set([0, 0, 1, 0, 0, 0, 0, 0, 0], 12);

  const state = parseDeviceState(payload);
  assert.equal(state.selectedSlot, 1);
  assert.equal(state.controllers[0].address, "01:02:03:04:05:06");
  assert.equal(state.controllers[0].gamepadIndex, 0);
  assert.equal(state.controllers[1].paired, false);
  assert.equal(state.controllers[1].gamepadIndex, 1);
});

test("OTA package validation checks header and declared body size", () => {
  const image = new Uint8Array(516);
  image.set(new TextEncoder().encode("BL60X_OTA"));
  image.set(new TextEncoder().encode("XZ"), 16);
  new DataView(image.buffer).setUint32(20, 4, true);
  assert.equal(validateOtaImage(image), 4);
  assert.throws(() => validateOtaImage(image.subarray(0, 515)), /Invalid/);
});

test("corrupt frame checksum is rejected", () => {
  const frame = encodeFrame(COMMAND.getDeviceState, 1);
  frame[5] ^= 0x01;
  assert.throws(() => decodeFrame(frame), /checksum/);
});
