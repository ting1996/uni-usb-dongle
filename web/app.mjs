import {
  CAPABILITY,
  addressBytes, parseDiscoveryResult, parseRadioState, headphoneStatus, radioErrorMessage,
  parseProfileState, parseAudioMetrics, profileLabel,
  COMMAND,
  OTA_HEADER_SIZE,
  STATUS,
  SerialProtocol,
  parseDeviceState,
  validateOtaImage,
} from "./protocol.mjs";
import { HidProtocol, HID_FILTERS } from "./hid-protocol.mjs";

const connectButton = document.querySelector("#connect");
const disconnectButton = document.querySelector("#disconnect");
const transportSelect = document.querySelector("#transport");
const connectionLabel = document.querySelector("#connection");
const statusLabel = document.querySelector("#device-status");
const outputProfileSelect = document.querySelector("#output-profile");
const applyProfileButton = document.querySelector("#apply-profile");
const profileStatus = document.querySelector("#profile-status");
const controllersElement = document.querySelector("#controllers");
const pairingStatus = document.querySelector("#pairing-status");
const audioStatus = document.querySelector("#audio-status");
const firmwareInput = document.querySelector("#firmware");
const updateButton = document.querySelector("#update");
const updateConsent = document.querySelector("#confirm-mfg");
const output = document.querySelector("#output");

let protocol;
let deviceState;
let profileState = null;
let updating = false;
let connecting = false;
let scanning = false;
let switchingProfile = false;
let radioPoll;
const discovered = document.querySelector("#discovered");

if (!("hid" in navigator) && "serial" in navigator) {
  transportSelect.value = "serial";
}

function updateControls() {
  transportSelect.disabled = !!protocol || updating || connecting;
  connectButton.disabled = updating || connecting || scanning;
  disconnectButton.disabled = !protocol || updating;
  const profileEditable = !!protocol && !updating && !connecting && !scanning &&
    !switchingProfile && profileState !== null;
  outputProfileSelect.disabled = !profileEditable;
  applyProfileButton.disabled = !profileEditable;
  firmwareInput.disabled = updating;
  updateConsent.disabled = updating;
  updateButton.disabled = !protocol || updating || scanning ||
    !firmwareInput.files.length || !updateConsent.checked;
  for (const button of controllersElement.querySelectorAll("button")) {
    if (updating) { button.disabled = true; }
  }
  const radioAvailable = !!protocol &&
    !!(deviceState?.capabilities & CAPABILITY.bluetoothHid);
  document.querySelector("#discover").disabled = updating || scanning || !radioAvailable;
  document.querySelector("#pair").disabled = updating || scanning || !radioAvailable || !discovered.value;
  document.querySelector("#pair-audio").disabled = updating || scanning || !radioAvailable || !discovered.value;
  discovered.disabled = updating || scanning || !radioAvailable;
}

async function disconnect() {
  clearInterval(radioPoll); scanning = false; switchingProfile = false;
  const previous = protocol;
  protocol = null;
  deviceState = null;
  profileState = null;
  if (previous) {
    try { await previous.close(); } catch { /* Device may have rebooted. */ }
  }
  connectionLabel.textContent = "Disconnected";
  connectButton.textContent = "Connect";
  controllersElement.replaceChildren();
  statusLabel.textContent = "No device status received.";
  profileStatus.textContent = "Connect to inspect USB output profile.";
  pairingStatus.textContent = "Connect to read controller slots.";
  audioStatus.textContent = "Connect to read audio capability.";
  updateControls();
}

function log(message) {
  output.textContent = `${new Date().toLocaleTimeString()} ${message}\n${output.textContent}`;
}

function statusName(status) {
  if (status === STATUS.busy) {
    return "Device is busy; try again after the update.";
  }
  if (status === STATUS.unsupported) {
    return "Not supported by this firmware";
  }
  if (status === STATUS.storageError) {
    return "Settings could not be saved";
  }
  if (status === STATUS.notFound) {
    return "Controller slot is empty";
  }
  return `Device returned status ${status}`;
}

async function request(command, payload) {
  const response = await protocol.request(command, payload);
  const status = response[0];
  if (status !== STATUS.ok) {
    throw new Error(statusName(status));
  }
  return response;
}

async function requestOptional(command, payload) {
  const response = await protocol.request(command, payload);
  const status = response[0];
  if (status === STATUS.invalidCommand || status === STATUS.unsupported) {
    return null;
  }
  if (status !== STATUS.ok) {
    throw new Error(statusName(status));
  }
  return response;
}

async function refreshProfileState() {
  const response = await requestOptional(COMMAND.getProfileState, new Uint8Array());
  if (!response) {
    profileState = null;
    profileStatus.textContent = "USB output profile switching is not supported by this firmware.";
    return;
  }
  profileState = parseProfileState(response);
  outputProfileSelect.value = String(profileState.active);
  const pendingNote = profileState.transition === 1 || profileState.transition === 2
    ? ` (switching to ${profileState.requestedLabel}…)`
    : profileState.failure
      ? ` (last switch failed: ${profileState.failure})`
      : "";
  profileStatus.textContent = `Active USB output profile: ${profileState.activeLabel}${pendingNote} (session-only; resets to Generic HID on power cycle).`;
}

async function refreshState() {
  const response = await request(COMMAND.getDeviceState, new Uint8Array());
  deviceState = parseDeviceState(response);
  statusLabel.textContent = `Selected slot: ${
    deviceState.selectedSlot === 0xff ? "None" : deviceState.selectedSlot + 1
  }`;
  pairingStatus.textContent = deviceState.capabilities & CAPABILITY.bluetoothHid
    ? "Bluetooth HID backend available"
    : "Bluetooth HID discovery/pairing is unavailable in this firmware build.";
  audioStatus.textContent = deviceState.capabilities & CAPABILITY.audio
    ? "Audio backend available"
    : "Bluetooth/USB audio streaming is not available in this firmware build.";
  document.querySelector("#discover").disabled =
    !(deviceState.capabilities & CAPABILITY.bluetoothHid);
  document.querySelector("#pair").disabled =
    !(deviceState.capabilities & CAPABILITY.bluetoothHid);
  await refreshProfileState();
  renderControllers();
  updateControls();
}

function renderControllers() {
  controllersElement.replaceChildren();
  for (const controller of deviceState.controllers) {
    const row = document.createElement("tr");
    const address = controller.paired ? controller.address : "—";
    const state = controller.connected ? "Connected" :
      controller.paired ? "Saved, disconnected" : "Empty";
    row.innerHTML = `
      <td>${controller.slot + 1}</td>
      <td>${address}</td>
      <td>${state}</td>
      <td>Gamepad ${controller.gamepadIndex + 1}</td>
      <td></td>`;
    const actions = row.lastElementChild;
    const select = document.createElement("button");
    select.textContent = "Connect";
    select.disabled = !controller.paired || updating;
    select.addEventListener("click", () => changeController(
      COMMAND.selectController, controller.slot,
    ));
    const remove = document.createElement("button");
    remove.textContent = "Remove";
    remove.disabled = !controller.paired || updating;
    remove.addEventListener("click", () => changeController(
      COMMAND.removeController, controller.slot,
    ));
    actions.append(select, remove);
    controllersElement.append(row);
  }
}

async function changeController(command, slot) {
  try {
    await request(command, new Uint8Array([slot]));
    if (command === COMMAND.selectController) {
      await request(COMMAND.pairController, addressBytes(deviceState.controllers[slot].address));
    }
    await refreshState();
    log(command === COMMAND.selectController
      ? `Selected controller slot ${slot + 1}.`
      : `Removed controller slot ${slot + 1}.`);
  } catch (error) {
    log(error.message);
  }
}

async function connect() {
  let next;
  if (transportSelect.value === "hid") {
    if (!("hid" in navigator)) {
      throw new Error("WebHID is unavailable. Use a supported desktop browser over HTTPS or localhost, or choose USB serial.");
    }
    const [device] = await navigator.hid.requestDevice({ filters: HID_FILTERS });
    if (!device) { return; }
    next = new HidProtocol(device);
  } else {
    if (!("serial" in navigator)) {
      throw new Error("Web Serial is unavailable in this browser.");
    }
    next = new SerialProtocol(await navigator.serial.requestPort());
  }
  try {
    await next.open();
    protocol = next;
    await refreshState();
  } catch (error) {
    try { await next.close(); } catch { /* Opening may have failed. */ }
    protocol = null;
    updateControls();
    throw error;
  }
  connectionLabel.textContent = `Connected over ${transportSelect.value === "hid" ? "WebHID" : "USB serial"}`;
  connectButton.textContent = "Refresh status";
  log("Connected to the dongle.");
  clearInterval(radioPoll);
  radioPoll = setInterval(() => { if (!updating && !scanning && protocol && (deviceState?.capabilities & CAPABILITY.bluetoothHid)) refreshRadio().catch(error => log(error.message)); }, 2500);
  updateControls();
}

async function sendOtaCommand(command, sequence, payload) {
  const response = await protocol.request(command, payload, 10000, sequence);
  if (response.length !== 5) {
    throw new Error("Invalid OTA response from device.");
  }
  const view = new DataView(response.buffer, response.byteOffset);
  const status = response[0];
  if (status !== STATUS.ok) {
    throw new Error(statusName(status));
  }
  return view.getUint32(1, true);
}

async function uploadFirmware() {
  if (!protocol || !firmwareInput.files.length) {
    throw new Error("Connect to the device and choose an OTA image first.");
  }
  if (!updateConsent.checked) {
    throw new Error("Acknowledge the manufacturing-data overlap before updating.");
  }

  const image = new Uint8Array(await firmwareInput.files[0].arrayBuffer());
  validateOtaImage(image);
  const begin = new Uint8Array(4 + OTA_HEADER_SIZE);
  new DataView(begin.buffer).setUint32(0, image.length, true);
  begin.set(image.subarray(0, OTA_HEADER_SIZE), 4);
  let offset = await sendOtaCommand(COMMAND.begin, 0, begin);
  if (offset !== OTA_HEADER_SIZE) {
    throw new Error(`Unexpected initial OTA offset ${offset}`);
  }

  try {
    for (; offset < image.length;) {
      const chunk = image.slice(offset, Math.min(offset + 512, image.length));
      const payload = await sendOtaCommand(COMMAND.data, offset, chunk);
      offset += chunk.length;
      if (payload !== offset) {
        throw new Error(`Unexpected acknowledged offset ${payload}`);
      }
      statusLabel.textContent =
        `Updating: ${Math.floor((offset / image.length) * 100)}%`;
    }
    const finalOffset = await sendOtaCommand(COMMAND.finish, image.length,
      new Uint8Array());
    if (finalOffset !== image.length) {
      throw new Error(`Unexpected final offset ${finalOffset}`);
    }
    log("OTA accepted; the device should reboot.");
    await disconnect();
  } catch (error) {
    try {
      await sendOtaCommand(COMMAND.abort, offset, new Uint8Array());
    } catch {
      log("OTA abort could not be confirmed; keep the device powered.");
    }
    throw error;
  }
}

connectButton.addEventListener("click", async () => {
  connecting = true;
  updateControls();
  try {
    if (protocol) {
      await refreshState();
      log("Device status refreshed.");
    } else {
      await connect();
    }
  } catch (error) {
    log(error.message);
    if (protocol?.closed) { await disconnect(); }
  } finally {
    connecting = false;
    updateControls();
  }
});

disconnectButton.addEventListener("click", () => disconnect());
if ("hid" in navigator) {
  navigator.hid.addEventListener("disconnect", (event) => {
    if (protocol instanceof HidProtocol && event.device === protocol.device) {
      log("Dongle disconnected or rebooted.");
      disconnect();
    }
  });
}
if ("serial" in navigator) {
  navigator.serial.addEventListener("disconnect", (event) => {
    if (protocol instanceof SerialProtocol &&
        (event.target === protocol.port || event.port === protocol.port)) {
      log("Dongle disconnected or rebooted.");
      disconnect();
    }
  });
}

async function refreshRadio() {
  const radio = parseRadioState(await request(COMMAND.radioState, new Uint8Array()));
  deviceState = parseDeviceState(await request(COMMAND.getDeviceState, new Uint8Array()));
  const metricsResp = await requestOptional(COMMAND.getAudioMetrics, new Uint8Array());
  const metrics = metricsResp ? parseAudioMetrics(metricsResp) : null;
  renderControllers();
  pairingStatus.textContent = radio.scanning ? "Discovering Bluetooth devices…" :
    `Radio ready · ${radio.count} discovered · ${radio.error ? `${radioErrorMessage(radio.error)} (${radio.error})` : "no radio error"}`;
  const metricsSummary = metrics
    ? ` · AVRCP ${metrics.avrcpConnected ? `connected (vol ${metrics.avrcpVolume}/127)` : "inactive"} · PCM peak L=${metrics.peakLeft} R=${metrics.peakRight} · nonzero ${metrics.nonzeroPackets}`
    : "";
  audioStatus.textContent = `${headphoneStatus(radio)} · USB ${radio.received} bytes · Bluetooth ${radio.sent} bytes · drops ${radio.overflows} · errors ${radio.errors}${metricsSummary}`;
  updateControls();
  return radio;
}

applyProfileButton.addEventListener("click", async () => {
  if (!protocol || !profileState) { return; }
  const targetProfile = Number(outputProfileSelect.value);
  switchingProfile = true;
  updateControls();
  try {
    const response = await request(
      COMMAND.setProfile,
      Uint8Array.of(targetProfile, profileState.codec ?? 0),
    );
    profileState = parseProfileState(response);
    profileStatus.textContent = `Switching USB output profile to ${profileLabel(targetProfile)}… Reconnect once re-enumeration completes.`;
    log(`Requested USB output profile ${profileLabel(targetProfile)}; dongle will re-enumerate briefly.`);
  } catch (error) {
    log(error.message);
  } finally {
    switchingProfile = false;
    updateControls();
  }
});

document.querySelector("#discover").addEventListener("click", async () => {
  scanning = true; updateControls();
  try {
    await request(COMMAND.startDiscovery, new Uint8Array());
    discovered.replaceChildren(new Option("Choose a device", ""));
    let radio;
    const deadline = Date.now() + 20000;
    do {
      await new Promise(resolve => setTimeout(resolve, 500));
      if (!protocol) return;
      radio = await refreshRadio();
    } while (radio.scanning && Date.now() < deadline);
    if (radio.scanning) throw new Error("Discovery is still running. Refresh and retry.");
    for (let i = 0; i < radio.count; ++i) {
      const device = parseDiscoveryResult(await request(COMMAND.discoveryResult, Uint8Array.of(i)));
      discovered.add(new Option(`${device.name || "Unnamed device"} · ${device.address} · ${device.rssi} dBm`, device.address));
    }
    log(`Discovery finished: ${radio.count} devices.`);
  } catch (error) { log(error.message); }
  finally { scanning = false; updateControls(); }
});

async function pairDevice(command) {
  try {
    await request(command, addressBytes(discovered.value));
    log(`Connecting to ${discovered.value}. Pairing completes on the dongle; refresh status to see the result.`);
    await new Promise(resolve => setTimeout(resolve, 2000));
    await refreshState(); await refreshRadio();
  } catch (error) { log(error.message); }
}
document.querySelector("#pair").addEventListener("click", () => pairDevice(COMMAND.pairController));
document.querySelector("#pair-audio").addEventListener("click", () => pairDevice(COMMAND.pairAudio));
discovered.addEventListener("change", updateControls);

firmwareInput.addEventListener("change", updateControls);
updateConsent.addEventListener("change", updateControls);
updateButton.addEventListener("click", async () => {
  updating = true;
  updateControls();
  try {
    await uploadFirmware();
  } catch (error) {
    log(error.message);
  } finally {
    updating = false;
    if (deviceState) { renderControllers(); }
    updateControls();
  }
});

updateControls();
