import test from "node:test";
import assert from "node:assert/strict";
import {
  COMMAND,
  OUTPUT_PROFILE,
  addressBytes,
  parseDiscoveryResult,
  parseRadioState,
  headphoneStatus,
  parseProfileState,
  parseAudioMetrics,
  profileLabel,
} from "../web/protocol.mjs";
test("Bluetooth discovery preserves address byte order, signed RSSI and bounded name", () => {
  const response = new Uint8Array(39);
  response.set([0,2,0x12,0x34,0x56,0x78,0x9a,0xbc,216]);
  response.set(new TextEncoder().encode("DualSense"),9);
  assert.deepEqual(parseDiscoveryResult(response), {index:2,address:"12:34:56:78:9A:BC",rssi:-40,name:"DualSense"});
  assert.deepEqual(addressBytes("12:34:56:78:9A:BC"),response.slice(2,8));
  assert.throws(() => addressBytes("12:34:56"));
  assert.throws(() => parseDiscoveryResult(response.slice(1)));
});
test("Radio status validates its payload and decodes audio counters", () => {
  const response = new Uint8Array(33);
  response.set([0,1,0,2,0,1,1]);
  new DataView(response.buffer).setUint32(13,192000,true);
  assert.equal(parseRadioState(response).received,192000);
  assert.equal(parseRadioState(response).streaming,true);
  response[0] = 3;
  assert.throws(() => parseRadioState(response));
});
test("Headphone status distinguishes saved pairing, connecting and streaming", () => {
  const legacy = new Uint8Array(33);
  legacy[4] = 111;
  assert.match(headphoneStatus(parseRadioState(legacy)), /not paired.*could not start.*111/);
  legacy.set([0x50,0x5e,0x5c,0xec,0xc1,0x23],7);
  assert.match(headphoneStatus(parseRadioState(legacy)), /Pairing saved; audio stream inactive/);
  const extended = new Uint8Array(44);
  extended.set(legacy);
  extended.set([3,0,0,0x50,0x5e,0x5c,0xec,0xc1,0x23,1,1],33);
  const parsed = parseRadioState(extended);
  assert.equal(parsed.audioAuthenticated,true);
  assert.match(headphoneStatus(parsed), /Paired; connecting audio profile/);
  extended[33] = 5;
  assert.match(headphoneStatus(parseRadioState(extended)), /Connected and streaming/);
  extended.set([6,1,111],33);
  assert.match(headphoneStatus(parseRadioState(extended)), /Connection failed.*connection:.*could not start.*111/);
  assert.throws(() => parseRadioState(extended.slice(0,43)));
});

test("Profile state and audio metrics commands decode firmware payloads", () => {
  assert.equal(COMMAND.getProfileState, 0x1e);
  assert.equal(COMMAND.setProfile, 0x1f);
  assert.equal(COMMAND.getAudioMetrics, 0x20);
  assert.equal(profileLabel(OUTPUT_PROFILE.ps4), "DualShock 4 (PS4)");
  assert.equal(profileLabel(OUTPUT_PROFILE.dualsense), "DualSense (PS5)");

  const profileBytes = Uint8Array.of(0, 1, OUTPUT_PROFILE.xinput, OUTPUT_PROFILE.ps4, 0x0f, 1, 0xff, 3, 1, 0, 0, 0);
  const profile = parseProfileState(profileBytes);
  assert.equal(profile.activeLabel, "Xbox 360 (XInput)");
  assert.equal(profile.requestedLabel, "DualShock 4 (PS4)");
  assert.equal(profile.streaming, null);
  assert.equal(profile.transition, 1);
  assert.throws(() => parseProfileState(Uint8Array.of(2)));

  const metricsBytes = new Uint8Array(12);
  metricsBytes[0] = 0;
  metricsBytes[1] = 1;
  metricsBytes[2] = 96;
  const view = new DataView(metricsBytes.buffer);
  view.setUint16(3, 8000, true);
  view.setUint16(5, 7950, true);
  view.setUint32(7, 1234, true);
  const metrics = parseAudioMetrics(metricsBytes);
  assert.deepEqual(metrics, {
    avrcpConnected: true,
    avrcpVolume: 96,
    peakLeft: 8000,
    peakRight: 7950,
    nonzeroPackets: 1234,
  });
  metricsBytes[0] = 2;
  assert.throws(() => parseAudioMetrics(metricsBytes));
});
