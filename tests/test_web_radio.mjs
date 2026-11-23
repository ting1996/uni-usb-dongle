import test from "node:test";
import assert from "node:assert/strict";
import {
  COMMAND,
  OUTPUT_PROFILE,
  MIC_SOURCE,
  MIC_CODEC,
  addressBytes,
  parseDiscoveryResult,
  parseRadioState,
  headphoneStatus,
  parseProfileState,
  parseAudioMetrics,
  parseDualsenseMetrics,
  parseGamepadDiagnostics,
  parseMicState,
  parseBlocklist,
  profileLabel,
  profileVidPid,
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
  assert.equal(profileVidPid(OUTPUT_PROFILE.dualsense), "054C:0CE6");
  assert.equal(profileVidPid(OUTPUT_PROFILE.xinput), "045E:028E");

  const profileBytes = Uint8Array.of(0, 1, OUTPUT_PROFILE.xinput, OUTPUT_PROFILE.ps4, 0x1f, 1, 0xff, 3, 1, 0, OUTPUT_PROFILE.dualsense, 0x03);
  const profile = parseProfileState(profileBytes);
  assert.equal(profile.activeLabel, "Xbox 360 (XInput)");
  assert.equal(profile.requestedLabel, "DualShock 4 (PS4)");
  assert.equal(profile.streaming, null);
  assert.equal(profile.transition, 1);
  assert.equal(profile.savedProfile, OUTPUT_PROFILE.dualsense);
  assert.equal(profile.persisted, true);
  assert.equal(profile.twoControllerMode, true);
  assert.throws(() => parseProfileState(Uint8Array.of(2)));

  const metricsBytes = new Uint8Array(12);
  metricsBytes[0] = 0;
  metricsBytes[1] = 0x07;
  metricsBytes[2] = 96;
  const view = new DataView(metricsBytes.buffer);
  view.setUint16(3, 8000, true);
  view.setUint16(5, 7950, true);
  view.setUint32(7, 1234, true);
  metricsBytes[11] = 0x05;
  const metrics = parseAudioMetrics(metricsBytes);
  assert.deepEqual(metrics, {
    avrcpConnected: true,
    hfpSlcReady: true,
    hfpScoOpen: true,
    hfpMsbcActive: false,
    avrcpVolume: 96,
    peakLeft: 8000,
    peakRight: 7950,
    nonzeroPackets: 1234,
    linkRoles: 0x05,
  });
  metricsBytes[0] = 2;
  assert.throws(() => parseAudioMetrics(metricsBytes));
});

test("v1.1 DualSense, gamepad diagnostics, mic config, and blocklist parsers decode payloads", () => {
  assert.equal(COMMAND.getGamepadDiagnostics, 0x16);
  assert.equal(COMMAND.getDualsenseMetrics, 0x21);
  assert.equal(COMMAND.getMicConfig, 0x22);
  assert.equal(COMMAND.setMicSource, 0x23);
  assert.equal(COMMAND.removeAudio, 0x24);
  assert.equal(COMMAND.getBlocklist, 0x25);
  assert.equal(COMMAND.blockDevice, 0x26);
  assert.equal(COMMAND.unblockDevice, 0x27);

  const dsLegacy = new Uint8Array(30);
  dsLegacy[2] = 0x2f;
  dsLegacy[3] = 0x27;
  new DataView(dsLegacy.buffer).setUint32(4, 120, true);
  const parsedLegacy = parseDualsenseMetrics(dsLegacy);
  assert.equal(parsedLegacy.btIn31, 120);
  assert.equal(parsedLegacy.lastValidFlag0, null);
  assert.equal(parsedLegacy.lastOutPathFlags, null);

  const dsV11 = new Uint8Array(38);
  dsV11.set(dsLegacy);
  dsV11.set([0x03, 0x0c, 0x80, 0x40, 0x01, 0x22, 0x05, 0x14], 30);
  const parsedV11 = parseDualsenseMetrics(dsV11);
  assert.equal(parsedV11.lastValidFlag0, 0x03);
  assert.equal(parsedV11.lastValidFlag1, 0x0c);
  assert.equal(parsedV11.lastMotorRight, 0x80);
  assert.equal(parsedV11.lastMotorLeft, 0x40);
  assert.equal(parsedV11.lastRightTriggerMode, 0x01);
  assert.equal(parsedV11.lastLeftTriggerMode, 0x22);
  assert.equal(parsedV11.lastOutPathFlags, 0x05);
  assert.equal(parsedV11.fwVerLo, 0x14);

  const gp = new Uint8Array(27);
  gp[1] = 0x01;
  gp.set([128, 128, 128, 128, 0, 0, 0, 0, 8], 2);
  new DataView(gp.buffer).setUint32(11, 50, true);
  new DataView(gp.buffer).setUint32(15, 49, true);
  const gpParsed = parseGamepadDiagnostics(gp);
  assert.equal(gpParsed.configured, true);
  assert.equal(gpParsed.submitted, 50);
  assert.equal(gpParsed.completed, 49);
  assert.equal(gpParsed.report.length, 9);

  const mic = new Uint8Array(16);
  mic[1] = MIC_SOURCE.auto;
  mic[2] = MIC_SOURCE.headphone;
  mic[3] = MIC_CODEC.msbc;
  mic[4] = 0x4d;
  const mv = new DataView(mic.buffer);
  mv.setUint16(6, 4200, true);
  mv.setUint32(8, 300, true);
  mv.setUint32(12, 2, true);
  const micParsed = parseMicState(mic);
  assert.equal(micParsed.configuredSource, MIC_SOURCE.auto);
  assert.equal(micParsed.activeSource, MIC_SOURCE.headphone);
  assert.equal(micParsed.activeCodec, MIC_CODEC.msbc);
  assert.equal(micParsed.usbOpen, true);
  assert.equal(micParsed.hfpScoOpen, true);
  assert.equal(micParsed.scoCallbackSeen, true);
  assert.equal(micParsed.micPeak, 4200);
  assert.equal(micParsed.decodedFrames, 300);
  assert.equal(micParsed.errorFrames, 2);

  const bl = Uint8Array.of(0, 2, 6, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
  assert.deepEqual(parseBlocklist(bl), ["AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"]);
  assert.throws(() => parseBlocklist(Uint8Array.of(0, 2, 6, 0xaa)));
});
