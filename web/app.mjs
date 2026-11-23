import {
  COMMAND,
  STATUS,
  OUTPUT_PROFILE,
  CAPABILITY,
  SerialProtocol,
  addressBytes,
  encodeFrame,
  headphoneStatus,
  micCodecLabel,
  micSourceLabel,
  parseAudioMetrics,
  parseBlocklist,
  parseDeviceState,
  parseDiscoveryResult,
  parseDualsenseMetrics,
  parseGamepadDiagnostics,
  parseMicState,
  parseProfileState,
  parseRadioState,
  profileLabel,
  profileVidPid,
  validateOtaImage,
} from "./protocol.mjs";
import { HidProtocol, HID_FILTERS } from "./hid-protocol.mjs";

const CHUNK_SIZE = 1024;

const el = (id) => document.getElementById(id);

const elements = {
  transport: el("transport"),
  connectBtn: el("connectBtn"),
  disconnectBtn: el("disconnectBtn"),
  refreshBtn: el("refreshBtn"),
  reconnectBannerBtn: el("reconnectBannerBtn"),
  connectionState: el("connectionState"),
  headerProfilePill: el("headerProfilePill"),
  headerCapabilities: el("headerCapabilities"),
  statusText: el("statusText"),
  tabButtons: Array.from(document.querySelectorAll(".tab-btn")),
  tabPanels: Array.from(document.querySelectorAll(".tabpanel")),

  overviewProfileName: el("overviewProfileName"),
  overviewProfileMeta: el("overviewProfileMeta"),
  overviewControllers: el("overviewControllers"),
  overviewControllersMeta: el("overviewControllersMeta"),
  overviewAudio: el("overviewAudio"),
  overviewAudioMeta: el("overviewAudioMeta"),
  overviewMic: el("overviewMic"),
  overviewMicMeta: el("overviewMicMeta"),

  profileTransitionBadge: el("profileTransitionBadge"),
  profileCards: Array.from(document.querySelectorAll("#profileCards .profile-option")),
  codecSelect: el("codecSelect"),
  persistProfileCheckbox: el("persistProfileCheckbox"),
  applyProfileBtn: el("applyProfileBtn"),
  profileStatusNote: el("profileStatusNote"),

  selectedSlot: el("selectedSlot"),
  controllers: el("controllers"),
  headphoneCard: el("headphoneCard"),
  discoverBtn: el("discoverBtn"),
  discoveryResults: el("discoveryResults"),
  blocklistNote: el("blocklistNote"),
  blockMacInput: el("blockMacInput"),
  blockMacBtn: el("blockMacBtn"),
  blocklistList: el("blocklistList"),

  micSourceSelect: el("micSourceSelect"),
  applyMicSourceBtn: el("applyMicSourceBtn"),
  micConfigured: el("micConfigured"),
  micActive: el("micActive"),
  micCodec: el("micCodec"),
  micHostState: el("micHostState"),
  micFrames: el("micFrames"),
  micPeakBar: el("micPeakBar"),
  micPeakText: el("micPeakText"),

  audioPlaybackState: el("audioPlaybackState"),
  audioAvrcpState: el("audioAvrcpState"),
  audioNonzero: el("audioNonzero"),
  audioBytes: el("audioBytes"),
  audioPeakLeftBar: el("audioPeakLeftBar"),
  audioPeakLeftText: el("audioPeakLeftText"),
  audioPeakRightBar: el("audioPeakRightBar"),
  audioPeakRightText: el("audioPeakRightText"),

  autoRefreshDiag: el("autoRefreshDiag"),
  refreshDiagBtn: el("refreshDiagBtn"),
  copyDiagBtn: el("copyDiagBtn"),
  diagRadioBox: el("diagRadioBox"),
  diagGamepadBox: el("diagGamepadBox"),
  diagDualsenseBox: el("diagDualsenseBox"),
  diagMicBox: el("diagMicBox"),

  otaFile: el("otaFile"),
  otaSummary: el("otaSummary"),
  mfgAck: el("mfgAck"),
  uploadBtn: el("uploadBtn"),
  abortBtn: el("abortBtn"),
  progress: el("progress"),
  progressText: el("progressText"),
  clearLogBtn: el("clearLogBtn"),
  log: el("log"),
};

let transportClient = null;
let connectedKind = "";
let capabilities = 0;
let busy = false;
let discovering = false;
let otaImage = null;
let selectedProfileChoice = OUTPUT_PROFILE.generic;
let discoveryEntries = [];
let diagTimer = null;
let snapshot = {
  connectedAt: null,
  transport: null,
  deviceState: null,
  profileState: null,
  radioState: null,
  audioMetrics: null,
  micState: null,
  blocklist: null,
  gamepadDiagnostics: [null, null],
  dualsenseMetrics: [null, null],
};

function appendLog(message) {
  const stamp = new Date().toLocaleTimeString();
  elements.log.textContent += `[${stamp}] ${message}\n`;
  elements.log.scrollTop = elements.log.scrollHeight;
}

function setStatus(message, { showReconnect = false } = {}) {
  elements.statusText.textContent = message;
  elements.reconnectBannerBtn.hidden = !showReconnect;
}

function activateTab(tabName) {
  for (const btn of elements.tabButtons) {
    const selected = btn.dataset.tab === tabName;
    btn.classList.toggle("active", selected);
    btn.setAttribute("aria-selected", selected ? "true" : "false");
    btn.tabIndex = selected ? 0 : -1;
  }
  for (const panel of elements.tabPanels) {
    panel.hidden = panel.dataset.panel !== tabName;
  }
}

function initTabs() {
  for (const btn of elements.tabButtons) {
    btn.addEventListener("click", () => activateTab(btn.dataset.tab));
    btn.addEventListener("keydown", (event) => {
      const idx = elements.tabButtons.indexOf(btn);
      let nextIdx = idx;
      if (event.key === "ArrowRight") nextIdx = (idx + 1) % elements.tabButtons.length;
      else if (event.key === "ArrowLeft") nextIdx = (idx + elements.tabButtons.length - 1) % elements.tabButtons.length;
      else if (event.key === "Home") nextIdx = 0;
      else if (event.key === "End") nextIdx = elements.tabButtons.length - 1;
      else return;
      event.preventDefault();
      const target = elements.tabButtons[nextIdx];
      target.focus();
      activateTab(target.dataset.tab);
    });
  }
}

function updateProfileCardSelection(profileId) {
  selectedProfileChoice = profileId;
  for (const card of elements.profileCards) {
    const isSel = Number(card.dataset.profile) === profileId;
    card.classList.toggle("selected", isSel);
    card.setAttribute("aria-checked", isSel ? "true" : "false");
  }
}

function capabilityNames(mask) {
  const names = [];
  if (mask & CAPABILITY.bluetoothHid) names.push("BT HID");
  if (mask & CAPABILITY.usbGamepad) names.push("USB Gamepad");
  if (mask & CAPABILITY.audio) names.push("USB Audio");
  return names.length ? names.join(" · ") : "None";
}

function triggerModeLabel(mode) {
  if (mode === null || mode === undefined) return "--";
  const map = {
    0x00: "Off (0x00)",
    0x01: "Continuous (0x01)",
    0x02: "Section (0x02)",
    0x05: "Off Reset (0x05)",
    0x21: "Feedback (0x21)",
    0x22: "Weapon (0x22)",
    0x26: "Vibration (0x26)",
  };
  return map[mode] || `0x${mode.toString(16).toUpperCase().padStart(2, "0")}`;
}

function outPathLabel(flags) {
  if (flags === null || flags === undefined) return "--";
  if (flags === 0) return "None";
  const parts = [];
  if (flags & 0x01) parts.push("EP OUT");
  if (flags & 0x02) parts.push("SET_REPORT");
  if (flags & 0x04) parts.push("Dedicated F1");
  if (flags & 0x08) parts.push("Translated");
  return `${parts.join(" + ")} (0x${flags.toString(16).toUpperCase().padStart(2, "0")})`;
}

function linkRoleLabel(code) {
  if (code === 1) return "Central";
  if (code === 2) return "Peripheral (scatternet!)";
  return "Disconnected";
}

function syncUiEnabled() {
  const connected = Boolean(transportClient);
  elements.transport.disabled = connected || busy;
  elements.connectBtn.disabled = connected || busy;
  elements.disconnectBtn.disabled = !connected || busy;
  elements.refreshBtn.disabled = !connected || busy;

  const canControl = connected && !busy;
  for (const card of elements.profileCards) {
    card.disabled = !canControl || !snapshot.profileState;
  }
  elements.codecSelect.disabled = !canControl || !snapshot.profileState;
  elements.persistProfileCheckbox.disabled = !canControl || !snapshot.profileState;
  elements.applyProfileBtn.disabled = !canControl || !snapshot.profileState;

  elements.discoverBtn.disabled = !canControl || discovering;
  const blockSupported = snapshot.blocklist !== null;
  elements.blockMacInput.disabled = !canControl || !blockSupported;
  elements.blockMacBtn.disabled = !canControl || !blockSupported;

  const micSupported = snapshot.micState !== null;
  elements.micSourceSelect.disabled = !canControl || !micSupported;
  elements.applyMicSourceBtn.disabled = !canControl || !micSupported;

  elements.refreshDiagBtn.disabled = !canControl;
  elements.copyDiagBtn.disabled = !connected;

  elements.otaFile.disabled = !connected || busy;
  elements.uploadBtn.disabled = !connected || busy || !otaImage || !elements.mfgAck.checked;
  elements.abortBtn.disabled = !connected || !busy;
}

function stopAutoRefresh() {
  if (diagTimer !== null) {
    clearInterval(diagTimer);
    diagTimer = null;
  }
}

function resetSnapshot() {
  snapshot = {
    connectedAt: null,
    transport: null,
    deviceState: null,
    profileState: null,
    radioState: null,
    audioMetrics: null,
    micState: null,
    blocklist: null,
    gamepadDiagnostics: [null, null],
    dualsenseMetrics: [null, null],
  };
  discoveryEntries = [];
}

function makeButton(label, handler, { disabled = false, variant = "" } = {}) {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.textContent = label;
  btn.disabled = disabled || busy;
  if (variant) btn.classList.add(variant);
  btn.addEventListener("click", handler);
  return btn;
}

function makePill(text, tone = "") {
  const span = document.createElement("span");
  span.className = `pill${tone ? ` ${tone}` : ""}`;
  span.textContent = text;
  return span;
}

function statusMessage(code) {
  if (code === STATUS.busy) return "busy (finish OTA or active scan first)";
  if (code === STATUS.notFound) return "device not found";
  if (code === STATUS.unsupported) return "blocked or unsupported by firmware";
  if (code === STATUS.invalidCommand) return "command unsupported on this firmware version";
  if (code === STATUS.storageError) return "flash storage error";
  if (code === STATUS.updateFailed) return "blocklist full or update rejected";
  return `error status ${code}`;
}

async function sendChecked(command, payload = new Uint8Array(), timeoutMs = 4000) {
  const reply = await transportClient.request(command, payload, timeoutMs);
  if (reply[0] !== STATUS.ok) {
    throw new Error(`Command 0x${command.toString(16)} failed: ${statusMessage(reply[0])}`);
  }
  return reply;
}

async function requestOptional(command, payload = new Uint8Array(), timeoutMs = 4000) {
  if (!transportClient) return null;
  try {
    const reply = await transportClient.request(command, payload, timeoutMs);
    if (reply[0] === STATUS.invalidCommand || reply[0] === STATUS.unsupported) {
      return null;
    }
    if (reply[0] !== STATUS.ok) {
      return null;
    }
    return reply;
  } catch {
    return null;
  }
}

function renderOverviewAndProfile() {
  const profile = snapshot.profileState;
  const devices = snapshot.deviceState;
  const radio = snapshot.radioState;
  const mic = snapshot.micState;

  if (!transportClient) {
    elements.connectionState.textContent = "Disconnected";
    elements.connectionState.className = "pill";
    elements.headerProfilePill.textContent = "Profile: --";
    elements.headerCapabilities.textContent = "Capabilities: --";
    elements.overviewProfileName.textContent = "--";
    elements.overviewProfileMeta.textContent = "Connect dongle to view profile";
    elements.overviewControllers.textContent = "--";
    elements.overviewControllersMeta.textContent = "Disconnected";
    elements.overviewAudio.textContent = "--";
    elements.overviewAudioMeta.textContent = "Disconnected";
    elements.overviewMic.textContent = "--";
    elements.overviewMicMeta.textContent = "Disconnected";
    elements.profileTransitionBadge.textContent = "Idle";
    elements.profileStatusNote.textContent = "Connect to inspect active and saved boot profile settings.";
    return;
  }

  elements.connectionState.textContent = `Connected (${connectedKind.toUpperCase()})`;
  elements.connectionState.className = "pill pill-ok";
  elements.headerCapabilities.textContent = capabilityNames(capabilities);

  if (profile) {
    const vidPid = profileVidPid(profile.active);
    const bootBadge = profile.persisted
      ? `Boot default: ${profile.savedProfileLabel}`
      : "Session default: Generic HID";
    elements.headerProfilePill.textContent = `${profile.activeLabel} (${vidPid})`;
    elements.overviewProfileName.textContent = profile.activeLabel;
    elements.overviewProfileMeta.textContent =
      `USB ${vidPid} · ${bootBadge} · ${profile.twoControllerMode ? "2-Controller mode" : "1-Controller mode"}`;

    const trLabels = ["Idle", "Switching...", "Applying...", "Failed"];
    elements.profileTransitionBadge.textContent = trLabels[profile.transition] || `State ${profile.transition}`;
    elements.profileStatusNote.textContent =
      `Active: ${profile.activeLabel} (${vidPid}). Saved boot default: ${profile.persisted ? profile.savedProfileLabel : "None (defaults to Generic HID)"}.`;
  } else {
    elements.headerProfilePill.textContent = "Profile: v1.0 firmware";
    elements.overviewProfileName.textContent = "Generic HID";
    elements.overviewProfileMeta.textContent = "Profile switching unavailable on this firmware";
    elements.profileStatusNote.textContent = "This firmware build does not support profile switching.";
  }

  if (devices) {
    const pairedCount = devices.controllers.filter((c) => c.paired).length;
    const connectedCount = devices.controllers.filter((c) => c.connected).length;
    elements.overviewControllers.textContent = `${connectedCount} / ${pairedCount} Connected`;
    const activeCtrl = devices.controllers[devices.selectedSlot];
    elements.overviewControllersMeta.textContent = activeCtrl && activeCtrl.paired
      ? `Selected: Slot ${devices.selectedSlot + 1} (${activeCtrl.address.toUpperCase()})`
      : pairedCount > 0 ? "Select an active controller slot" : "No controllers paired";
  }

  if (radio) {
    elements.overviewAudio.textContent = radio.streaming ? "Streaming SBC" :
      radio.headphone !== "00:00:00:00:00:00" ? "Headphone Saved" : "Not Paired";
    elements.overviewAudioMeta.textContent = headphoneStatus(radio);
  }

  if (mic) {
    elements.overviewMic.textContent = mic.usbOpen
      ? `${mic.activeSourceLabel}` : `Standby (${mic.configuredSourceLabel})`;
    elements.overviewMicMeta.textContent = mic.usbOpen
      ? `${mic.activeCodecLabel} · Peak ${mic.micPeak}`
      : `Codec: ${mic.activeCodecLabel} · Host mic closed`;
  } else {
    elements.overviewMic.textContent = "Requires v1.1.0";
    elements.overviewMicMeta.textContent = "Upgrade firmware for Bluetooth mic support";
  }
}

function renderControllers() {
  elements.controllers.replaceChildren();
  const state = snapshot.deviceState;
  if (!state) {
    elements.selectedSlot.textContent = "--";
    const p = document.createElement("p");
    p.className = "muted";
    p.textContent = "Connect to inspect controller slots.";
    elements.controllers.append(p);
    return;
  }

  elements.selectedSlot.textContent = state.selectedSlot === 0xff
    ? "None" : `Slot ${state.selectedSlot + 1}`;
  elements.controllers.className = "device-list";

  for (const c of state.controllers) {
    const row = document.createElement("div");
    row.className = "device-row";

    const info = document.createElement("div");
    info.className = "device-row-info";
    const title = document.createElement("strong");
    title.textContent = c.paired
      ? `Slot ${c.slot + 1} · ${c.address.toUpperCase()}`
      : `Slot ${c.slot + 1} · Empty`;
    const badges = document.createElement("div");
    badges.className = "pill-row";
    if (c.paired) {
      badges.append(makePill(c.connected ? "Connected" : "Paired / Offline", c.connected ? "pill-ok" : ""));
      if (state.selectedSlot === c.slot) {
        badges.append(makePill("Selected", "pill-accent"));
      }
      badges.append(makePill(`USB Gamepad ${c.gamepadIndex + 1}`));
    } else {
      badges.append(makePill("Available for pairing"));
    }
    info.append(title, badges);

    const actions = document.createElement("div");
    actions.className = "device-row-actions";
    if (c.paired) {
      actions.append(
        makeButton("Select", () => handleSelectSlot(c.slot), {
          disabled: state.selectedSlot === c.slot,
        }),
        makeButton("Disconnect", () => handleDisconnectAddress(c.address), {
          disabled: !c.connected,
        }),
        makeButton("Remove", () => handleRemoveSlot(c.slot)),
        makeButton("Never Connect", () => handleBlockAddress(c.address), {
          disabled: snapshot.blocklist === null,
          variant: "btn-danger",
        }),
      );
    }

    row.append(info, actions);
    elements.controllers.append(row);
  }
}

function renderHeadphone() {
  elements.headphoneCard.replaceChildren();
  const radio = snapshot.radioState;
  const audio = snapshot.audioMetrics;
  if (!radio) {
    elements.headphoneCard.textContent = "Connect to inspect paired Bluetooth headphone.";
    return;
  }

  const row = document.createElement("div");
  row.className = "device-row";

  const info = document.createElement("div");
  info.className = "device-row-info";
  const title = document.createElement("strong");
  const hpAddr = radio.headphone && radio.headphone !== "00:00:00:00:00:00"
    ? radio.headphone : null;
  title.textContent = hpAddr ? `Headphone · ${hpAddr}` : "No Bluetooth headphone paired";

  const sub = document.createElement("p");
  sub.className = "muted";
  sub.textContent = headphoneStatus(radio);

  const badges = document.createElement("div");
  badges.className = "pill-row";
  if (audio) {
    if (audio.avrcpConnected) badges.append(makePill("AVRCP Ready", "pill-accent"));
    if (audio.hfpSlcReady) badges.append(makePill(audio.hfpScoOpen ? "HFP SCO Active" : "HFP Mic Ready", "pill-ok"));
  }
  info.append(title, sub, badges);

  const actions = document.createElement("div");
  actions.className = "device-row-actions";
  const targetAddr = hpAddr || (radio.audioTarget && radio.audioTarget !== "00:00:00:00:00:00" ? radio.audioTarget : null);
  actions.append(
    makeButton("Disconnect", () => handleDisconnectAddress(targetAddr), {
      disabled: !targetAddr,
    }),
    makeButton("Remove", () => handleRemoveAudio(), {
      disabled: !hpAddr && !radio.audioConnected,
    }),
    makeButton("Never Connect", () => handleBlockAddress(targetAddr), {
      disabled: !targetAddr || snapshot.blocklist === null,
      variant: "btn-danger",
    }),
  );

  row.append(info, actions);
  elements.headphoneCard.append(row);
}

function renderDiscovery() {
  elements.discoveryResults.replaceChildren();
  if (!discoveryEntries.length) {
    const p = document.createElement("p");
    p.className = "muted";
    p.textContent = discovering
      ? "Scanning nearby Bluetooth devices..."
      : "No nearby devices listed. Click Scan Nearby Devices while a device is in pairing mode.";
    elements.discoveryResults.append(p);
    return;
  }

  const blockedSet = new Set(snapshot.blocklist || []);
  for (const entry of discoveryEntries) {
    const row = document.createElement("div");
    row.className = "device-row";

    const info = document.createElement("div");
    info.className = "device-row-info";
    const title = document.createElement("strong");
    title.textContent = `${entry.name || "Unnamed device"} · ${entry.address}`;
    const meta = document.createElement("span");
    meta.className = "muted";
    meta.textContent = `RSSI ${entry.rssi} dBm`;
    info.append(title, meta);

    const actions = document.createElement("div");
    actions.className = "device-row-actions";
    const isBlocked = blockedSet.has(entry.address);
    if (isBlocked) {
      actions.append(makePill("Blocked (Never Connect)", "pill-danger"));
      actions.append(makeButton("Unblock", () => handleUnblockAddress(entry.address)));
    } else {
      actions.append(
        makeButton("Pair Controller", () => handlePairDevice(COMMAND.pairController, entry.address, "controller"), {
          disabled: !(capabilities & CAPABILITY.bluetoothHid),
        }),
        makeButton("Pair Headphone", () => handlePairDevice(COMMAND.pairAudio, entry.address, "headphone"), {
          disabled: !(capabilities & CAPABILITY.bluetoothHid),
        }),
        makeButton("Never Connect", () => handleBlockAddress(entry.address), {
          disabled: snapshot.blocklist === null,
          variant: "btn-danger",
        }),
      );
    }

    row.append(info, actions);
    elements.discoveryResults.append(row);
  }
}

function renderBlocklist() {
  elements.blocklistList.replaceChildren();
  if (!transportClient) {
    elements.blocklistNote.textContent = "Connect dongle to view and manage blocked Bluetooth addresses.";
    return;
  }
  if (snapshot.blocklist === null) {
    elements.blocklistNote.textContent = "Never Connect blocklist requires v1.1.0+ firmware.";
    return;
  }
  elements.blocklistNote.textContent =
    `Blocked MAC addresses (${snapshot.blocklist.length} / 6 used). Blocked devices cannot pair or auto-reconnect.`;
  if (snapshot.blocklist.length === 0) {
    const p = document.createElement("p");
    p.className = "muted";
    p.textContent = "Blocklist is empty.";
    elements.blocklistList.append(p);
    return;
  }

  for (const addr of snapshot.blocklist) {
    const row = document.createElement("div");
    row.className = "device-row";
    const label = document.createElement("strong");
    label.textContent = addr;
    const unblockBtn = makeButton("Unblock", () => handleUnblockAddress(addr));
    row.append(label, unblockBtn);
    elements.blocklistList.append(row);
  }
}

function renderAudioAndMic() {
  const mic = snapshot.micState;
  const audio = snapshot.audioMetrics;
  const radio = snapshot.radioState;

  if (mic) {
    elements.micSourceSelect.value = String(mic.configuredSource);
    elements.micConfigured.textContent = mic.configuredSourceLabel;
    elements.micActive.textContent = mic.activeSourceLabel;
    elements.micCodec.textContent = mic.activeCodecLabel;
    elements.micHostState.textContent = mic.usbOpen
      ? (mic.usbMuted ? "Open (Muted by host)" : "Capturing (16 kHz Mono)")
      : "Closed (Idle)";
    elements.micFrames.textContent = `${mic.decodedFrames} decoded / ${mic.errorFrames} errors`;
    elements.micPeakBar.value = Math.min(32767, mic.micPeak);
    elements.micPeakText.textContent = String(mic.micPeak);
  } else {
    elements.micConfigured.textContent = transportClient ? "Requires v1.1.0" : "--";
    elements.micActive.textContent = "--";
    elements.micCodec.textContent = "--";
    elements.micHostState.textContent = "--";
    elements.micFrames.textContent = "--";
    elements.micPeakBar.value = 0;
    elements.micPeakText.textContent = "0";
  }

  if (audio) {
    elements.audioPlaybackState.textContent = radio && radio.streaming ? "Streaming A2DP SBC" : "Idle / Standby";
    elements.audioAvrcpState.textContent = audio.avrcpConnected
      ? `Connected (Vol ${audio.avrcpVolume}/127)` : "Disconnected";
    elements.audioNonzero.textContent = String(audio.nonzeroPackets);
    elements.audioPeakLeftBar.value = Math.min(32767, audio.peakLeft);
    elements.audioPeakRightBar.value = Math.min(32767, audio.peakRight);
    elements.audioPeakLeftText.textContent = String(audio.peakLeft);
    elements.audioPeakRightText.textContent = String(audio.peakRight);
  } else {
    elements.audioPlaybackState.textContent = "--";
    elements.audioAvrcpState.textContent = "--";
    elements.audioNonzero.textContent = "--";
    elements.audioPeakLeftBar.value = 0;
    elements.audioPeakRightBar.value = 0;
    elements.audioPeakLeftText.textContent = "0";
    elements.audioPeakRightText.textContent = "0";
  }

  elements.audioBytes.textContent = radio
    ? `${radio.received} RX / ${radio.sent} BT (U:${radio.underflows} O:${radio.overflows} E:${radio.errors})`
    : "--";
}

function makeKvTable(rows) {
  const table = document.createElement("table");
  table.className = "kv-table";
  for (const [k, v] of rows) {
    const tr = document.createElement("tr");
    const th = document.createElement("th");
    th.textContent = k;
    const td = document.createElement("td");
    td.textContent = String(v);
    tr.append(th, td);
    table.append(tr);
  }
  return table;
}

function renderDiagnostics() {
  const radio = snapshot.radioState;
  const audio = snapshot.audioMetrics;
  const mic = snapshot.micState;

  elements.diagRadioBox.replaceChildren();
  if (!radio) {
    const p = document.createElement("p");
    p.className = "muted";
    p.textContent = "Connect to inspect radio diagnostics.";
    elements.diagRadioBox.append(p);
  } else {
    const roles = audio ? audio.linkRoles : 0;
    const hpRole = roles & 0x03;
    const c0Role = (roles >> 2) & 0x03;
    const c1Role = (roles >> 4) & 0x03;
    const radioCard = document.createElement("div");
    radioCard.className = "diag-card";
    const h3 = document.createElement("h3");
    h3.textContent = "Bluetooth Radio & Link Roles";
    radioCard.append(h3, makeKvTable([
      ["Radio Ready / Scanning", `${radio.ready} / ${radio.scanning}`],
      ["Controller Mask", `0x${radio.connectedMask.toString(16).padStart(2, "0")}`],
      ["Headphone ACL Role", linkRoleLabel(hpRole)],
      ["Controller 1 / 2 Role", `${linkRoleLabel(c0Role)} / ${linkRoleLabel(c1Role)}`],
      ["Last Radio Error", radio.error],
    ]));

    const audioCard = document.createElement("div");
    audioCard.className = "diag-card";
    const h3a = document.createElement("h3");
    h3a.textContent = "USB Audio & HFP Link";
    audioCard.append(h3a, makeKvTable([
      ["USB RX / BT TX Bytes", `${radio.received} / ${radio.sent}`],
      ["Underflow / Overflow / Err", `${radio.underflows} / ${radio.overflows} / ${radio.errors}`],
      ["HFP SLC Ready / SCO Open", audio ? `${audio.hfpSlcReady} / ${audio.hfpScoOpen}` : "--"],
      ["HFP Wideband (mSBC)", audio ? String(audio.hfpMsbcActive) : "--"],
    ]));
    elements.diagRadioBox.append(radioCard, audioCard);
  }

  elements.diagGamepadBox.replaceChildren();
  for (let slot = 0; slot < 2; slot += 1) {
    const gp = snapshot.gamepadDiagnostics[slot];
    const card = document.createElement("div");
    card.className = "diag-card";
    const title = document.createElement("h3");
    title.textContent = `Gamepad Endpoint Slot ${slot + 1}`;
    if (!gp) {
      const p = document.createElement("p");
      p.className = "muted";
      p.textContent = transportClient ? "No data returned" : "Disconnected";
      card.append(title, p);
    } else {
      const hex = gp.report.map((b) => b.toString(16).padStart(2, "0")).join(" ");
      card.append(title, makeKvTable([
        ["Configured / Busy / Pending", `${gp.configured} / ${gp.busy} / ${gp.pending}`],
        ["Submitted / Completed", `${gp.submitted} / ${gp.completed}`],
        ["Tick / Test Active", `${gp.tickCount} / ${gp.testActive}`],
        ["Canonical Report (9B)", hex],
      ]));
    }
    elements.diagGamepadBox.append(card);
  }

  elements.diagDualsenseBox.replaceChildren();
  for (let slot = 0; slot < 2; slot += 1) {
    const ds = snapshot.dualsenseMetrics[slot];
    const card = document.createElement("div");
    card.className = "diag-card";
    const title = document.createElement("h3");
    title.textContent = `DualSense Slot ${slot + 1}`;
    if (!ds) {
      const p = document.createElement("p");
      p.className = "muted";
      p.textContent = transportClient ? "Unavailable" : "Disconnected";
      card.append(title, p);
    } else {
      card.append(title, makeKvTable([
        ["BT L2CAP (ACL/Auth/Ctrl/Int)", `${ds.aclConnected}/${ds.authenticated}/${ds.controlOpen}/${ds.interruptOpen}`],
        ["LED Unlocked / Probe Mask", `${ds.ledUnlocked} / 0x${ds.featMask.toString(16).padStart(2, "0")}`],
        ["BT IN (0x31 / 0x01)", `${ds.btIn31} / ${ds.btIn01}`],
        ["USB OUT 0x02 Received", ds.usbOut02],
        ["BT OUT 0x31 (OK / Err)", `${ds.btOut31Ok} / ${ds.btOut31Err}`],
        ["Feature Set / Get OK", `${ds.btFeatSetOk} / ${ds.btFeatGetOk}`],
        ["Last valid_flag0 / flag1", ds.lastValidFlag0 === null ? "Requires v1.1.0" :
          `0x${ds.lastValidFlag0.toString(16).padStart(2, "0")} / 0x${ds.lastValidFlag1.toString(16).padStart(2, "0")}`],
        ["Motors (Right / Left)", ds.lastMotorRight === null ? "--" :
          `${ds.lastMotorRight} / ${ds.lastMotorLeft}`],
        ["Triggers (R2 / L2 Mode)", ds.lastRightTriggerMode === null ? "--" :
          `${triggerModeLabel(ds.lastRightTriggerMode)} / ${triggerModeLabel(ds.lastLeftTriggerMode)}`],
        ["OUT Path / FW Lo", ds.lastOutPathFlags === null ? "--" :
          `${outPathLabel(ds.lastOutPathFlags)} · FW 0x${(ds.fwVerLo || 0).toString(16).padStart(2, "0")}`],
      ]));
    }
    elements.diagDualsenseBox.append(card);
  }

  elements.diagMicBox.replaceChildren();
  const micCard = document.createElement("div");
  micCard.className = "diag-card";
  const micH3 = document.createElement("h3");
  micH3.textContent = "Microphone & SCO Decoder";
  if (!mic) {
    const p = document.createElement("p");
    p.className = "muted";
    p.textContent = transportClient ? "Requires v1.1.0+ firmware" : "Disconnected";
    micCard.append(micH3, p);
  } else {
    micCard.append(micH3, makeKvTable([
      ["Configured / Active", `${mic.configuredSourceLabel} / ${mic.activeSourceLabel}`],
      ["Active Codec", mic.activeCodecLabel],
      ["USB Open / Muted", `${mic.usbOpen} / ${mic.usbMuted}`],
      ["HFP SLC / SCO / Callback Seen", `${mic.hfpSlcReady} / ${mic.hfpScoOpen} / ${mic.scoCallbackSeen}`],
      ["DualSense Mask (S1 / S2)", `${mic.ds0Connected} / ${mic.ds1Connected}`],
      ["Peak / Decoded / Errors", `${mic.micPeak} / ${mic.decodedFrames} / ${mic.errorFrames}`],
    ]));
  }
  elements.diagMicBox.append(micCard);
}

function renderAll() {
  renderOverviewAndProfile();
  renderControllers();
  renderHeadphone();
  renderDiscovery();
  renderBlocklist();
  renderAudioAndMic();
  renderDiagnostics();
  syncUiEnabled();
}

async function refreshMetricsAndDiag() {
  if (!transportClient || busy) return;
  try {
    const radioRaw = await requestOptional(COMMAND.radioState);
    if (radioRaw) snapshot.radioState = parseRadioState(radioRaw);

    const audioRaw = await requestOptional(COMMAND.getAudioMetrics);
    if (audioRaw) snapshot.audioMetrics = parseAudioMetrics(audioRaw);

    const micRaw = await requestOptional(COMMAND.getMicConfig);
    if (micRaw) snapshot.micState = parseMicState(micRaw);

    const gp0 = await requestOptional(COMMAND.getGamepadDiagnostics, Uint8Array.of(0));
    const gp1 = await requestOptional(COMMAND.getGamepadDiagnostics, Uint8Array.of(1));
    snapshot.gamepadDiagnostics = [
      gp0 ? parseGamepadDiagnostics(gp0) : null,
      gp1 ? parseGamepadDiagnostics(gp1) : null,
    ];

    const ds0 = await requestOptional(COMMAND.getDualsenseMetrics, Uint8Array.of(0));
    const ds1 = await requestOptional(COMMAND.getDualsenseMetrics, Uint8Array.of(1));
    snapshot.dualsenseMetrics = [
      ds0 ? parseDualsenseMetrics(ds0) : null,
      ds1 ? parseDualsenseMetrics(ds1) : null,
    ];

    renderOverviewAndProfile();
    renderHeadphone();
    renderAudioAndMic();
    renderDiagnostics();
  } catch (error) {
    appendLog(`Refresh metrics warning: ${error.message}`);
  }
}

async function refreshAll() {
  if (!transportClient) {
    renderAll();
    return;
  }
  const devRaw = await sendChecked(COMMAND.getDeviceState);
  snapshot.deviceState = parseDeviceState(devRaw);
  capabilities = snapshot.deviceState.capabilities;

  const profRaw = await requestOptional(COMMAND.getProfileState);
  if (profRaw) {
    snapshot.profileState = parseProfileState(profRaw);
    updateProfileCardSelection(snapshot.profileState.requested);
    elements.codecSelect.value = String(snapshot.profileState.codec > 1 ? 0 : snapshot.profileState.codec);
  } else {
    snapshot.profileState = null;
  }

  const blRaw = await requestOptional(COMMAND.getBlocklist);
  snapshot.blocklist = blRaw ? parseBlocklist(blRaw) : null;

  await refreshMetricsAndDiag();
  renderAll();
}

async function openClient(client, kind) {
  await client.open();
  transportClient = client;
  connectedKind = kind;
  snapshot.connectedAt = new Date().toISOString();
  snapshot.transport = kind;
  await refreshAll();
  setStatus(`Connected via ${kind.toUpperCase()}.`);
  appendLog(`Connected via ${kind.toUpperCase()}.`);
}

async function connectInteractive() {
  const kind = elements.transport.value;
  if (kind === "hid") {
    if (!("hid" in navigator)) {
      throw new Error("WebHID is unavailable in this desktop browser.");
    }
    const [device] = await navigator.hid.requestDevice({ filters: HID_FILTERS });
    if (!device) return false;
    await openClient(new HidProtocol(device), "hid");
    return true;
  }
  if (!("serial" in navigator)) {
    throw new Error("Web Serial is unavailable in this browser.");
  }
  const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0xffff }] });
  await openClient(new SerialProtocol(port), "serial");
  return true;
}

async function tryAutoReconnectHid() {
  if (!("hid" in navigator)) return false;
  const authorized = await navigator.hid.getDevices();
  for (const dev of authorized) {
    const matchesFilter = HID_FILTERS.some(
      (f) => f.vendorId === dev.vendorId && f.productId === dev.productId,
    );
    const hasMgmtCollection = dev.collections?.some(
      (c) => c.usagePage === 0xff00 && c.usage === 1,
    );
    if (!matchesFilter || !hasMgmtCollection) continue;
    try {
      await openClient(new HidProtocol(dev), "hid");
      return true;
    } catch {
      /* Continue trying next authorized HID node */
    }
  }
  return false;
}

async function disconnectClient(reasonText = "Disconnected.") {
  stopAutoRefresh();
  elements.autoRefreshDiag.checked = false;
  const client = transportClient;
  transportClient = null;
  connectedKind = "";
  capabilities = 0;
  discovering = false;
  resetSnapshot();
  if (client) {
    try {
      await client.close();
    } catch {
      /* Port may already be detached during USB re-enumeration */
    }
  }
  setStatus(reasonText);
  renderAll();
}

async function handleApplyProfile() {
  if (!transportClient || !snapshot.profileState || busy) return;
  const targetProfile = selectedProfileChoice;
  const codec = Number(elements.codecSelect.value);
  const persist = elements.persistProfileCheckbox.checked ? 1 : 0;
  const sameProfile = snapshot.profileState.active === targetProfile;

  const reply = await sendChecked(
    COMMAND.setProfile,
    Uint8Array.of(targetProfile, codec, persist),
  );
  const updated = parseProfileState(reply);
  snapshot.profileState = updated;
  appendLog(
    `Profile requested: ${profileLabel(targetProfile)} (${profileVidPid(targetProfile)}), persist=${Boolean(persist)}`,
  );

  if (sameProfile && updated.transition === 0) {
    await refreshAll();
    setStatus(
      `Saved ${updated.activeLabel} preferences${persist ? " as boot default" : ""}.`,
    );
    return;
  }

  setStatus(`Switching USB profile to ${profileLabel(targetProfile)}; waiting for re-enumeration...`);
  const prevKind = connectedKind;
  await new Promise((r) => setTimeout(r, 1500));
  if (transportClient) {
    try {
      await transportClient.close();
    } catch {
      /* Expected during USB disconnect */
    }
    transportClient = null;
  }

  let reconnected = false;
  if (prevKind === "hid") {
    for (let attempt = 0; attempt < 4; attempt += 1) {
      await new Promise((r) => setTimeout(r, 500));
      if (await tryAutoReconnectHid()) {
        reconnected = true;
        break;
      }
    }
  }

  if (reconnected) {
    setStatus(`Profile switched to ${profileLabel(targetProfile)} and reconnected automatically.`);
  } else {
    connectedKind = "";
    resetSnapshot();
    renderAll();
    setStatus(
      `Dongle switched to ${profileLabel(targetProfile)} (${profileVidPid(targetProfile)}). Click Reconnect Dongle to grant access to the new USB ID.`,
      { showReconnect: true },
    );
  }
}

async function handleSelectSlot(slot) {
  await sendChecked(COMMAND.selectController, Uint8Array.of(slot));
  appendLog(`Selected controller Slot ${slot + 1}.`);
  await refreshAll();
}

async function handleRemoveSlot(slot) {
  await sendChecked(COMMAND.removeController, Uint8Array.of(slot));
  appendLog(`Removed controller Slot ${slot + 1}.`);
  await refreshAll();
}

async function handleRemoveAudio() {
  const reply = await requestOptional(COMMAND.removeAudio);
  if (!reply) {
    setStatus("Remove paired headphone requires v1.1.0+ firmware.");
    return;
  }
  appendLog("Removed paired Bluetooth headphone.");
  await refreshAll();
}

async function handleDisconnectAddress(address) {
  if (!address) return;
  await sendChecked(COMMAND.disconnectPeer, addressBytes(address));
  appendLog(`Requested disconnect for ${address}.`);
  await refreshAll();
}

async function handlePairDevice(command, address, roleName) {
  await sendChecked(command, addressBytes(address));
  appendLog(`Pairing ${roleName} ${address}...`);
  setStatus(`Connecting ${roleName} ${address}...`);
  await refreshAll();
}

async function handleBlockAddress(address) {
  if (!address) return;
  const reply = await sendChecked(COMMAND.blockDevice, addressBytes(address));
  snapshot.blocklist = parseBlocklist(reply);
  appendLog(`Blocked ${address} (Never Connect).`);
  await refreshAll();
}

async function handleUnblockAddress(address) {
  const reply = await sendChecked(COMMAND.unblockDevice, addressBytes(address));
  snapshot.blocklist = parseBlocklist(reply);
  appendLog(`Unblocked ${address}.`);
  await refreshAll();
}

async function handleDiscover() {
  if (!transportClient || busy || discovering) return;
  discovering = true;
  discoveryEntries = [];
  renderDiscovery();
  syncUiEnabled();
  setStatus("Scanning nearby Bluetooth devices (about 7 seconds)...");
  try {
    await sendChecked(COMMAND.startDiscovery);
    const deadline = Date.now() + 20000;
    let count = 0;
    while (Date.now() < deadline && transportClient) {
      await new Promise((r) => setTimeout(r, 800));
      const radio = parseRadioState(await sendChecked(COMMAND.radioState));
      snapshot.radioState = radio;
      count = radio.count;
      if (!radio.scanning) break;
    }
    const entries = [];
    for (let i = 0; i < count; i += 1) {
      entries.push(parseDiscoveryResult(await sendChecked(COMMAND.discoveryResult, Uint8Array.of(i))));
    }
    entries.sort((a, b) => b.rssi - a.rssi);
    discoveryEntries = entries;
    appendLog(`Discovery found ${entries.length} device(s).`);
    setStatus(`Discovery complete: ${entries.length} device(s) found.`);
  } finally {
    discovering = false;
    renderDiscovery();
    syncUiEnabled();
  }
}

async function handleApplyMicSource() {
  const src = Number(elements.micSourceSelect.value);
  const reply = await sendChecked(COMMAND.setMicSource, Uint8Array.of(src));
  snapshot.micState = parseMicState(reply);
  appendLog(`Microphone source set to ${micSourceLabel(src)}.`);
  setStatus(`Microphone source saved: ${micSourceLabel(src)}.`);
  renderOverviewAndProfile();
  renderAudioAndMic();
  renderDiagnostics();
}

async function handleUploadOta() {
  if (!transportClient || !otaImage) return;
  busy = true;
  stopAutoRefresh();
  elements.autoRefreshDiag.checked = false;
  syncUiEnabled();

  const bodySize = validateOtaImage(otaImage);
  try {
    const view = new DataView(new ArrayBuffer(4));
    view.setUint32(0, otaImage.length, true);
    const beginPayload = new Uint8Array(4 + 512);
    beginPayload.set(new Uint8Array(view.buffer), 0);
    beginPayload.set(otaImage.subarray(0, 512), 4);
    await sendChecked(COMMAND.begin, beginPayload, 10000);
    appendLog(`OTA started (${otaImage.length} B, body ${bodySize} B).`);

    let offset = 512;
    while (offset < otaImage.length) {
      const end = Math.min(offset + CHUNK_SIZE, otaImage.length);
      const header = new DataView(new ArrayBuffer(4));
      header.setUint32(0, offset, true);
      const chunkPayload = new Uint8Array(4 + (end - offset));
      chunkPayload.set(new Uint8Array(header.buffer), 0);
      chunkPayload.set(otaImage.subarray(offset, end), 4);
      await sendChecked(COMMAND.data, chunkPayload, 10000);
      offset = end;
      const ratio = offset / otaImage.length;
      elements.progress.value = ratio;
      elements.progressText.textContent = `${Math.round(ratio * 100)}%`;
      setStatus(`Uploading firmware: ${offset} / ${otaImage.length} bytes`);
    }

    await sendChecked(COMMAND.finish, new Uint8Array(), 15000);
    appendLog("Firmware update applied; device is rebooting.");
    await disconnectClient("Firmware updated. Wait 3 seconds and click Connect.");
  } catch (error) {
    appendLog(`OTA error: ${error.message}`);
    setStatus(`OTA failed: ${error.message}`);
  } finally {
    busy = false;
    syncUiEnabled();
  }
}

async function handleAbortOta() {
  if (!transportClient) return;
  try {
    await sendChecked(COMMAND.abort);
    appendLog("OTA aborted.");
    setStatus("OTA aborted.");
  } catch (error) {
    appendLog(`Abort warning: ${error.message}`);
  } finally {
    busy = false;
    syncUiEnabled();
  }
}

function wireEvents() {
  initTabs();

  for (const card of elements.profileCards) {
    card.addEventListener("click", () => {
      if (!card.disabled) {
        updateProfileCardSelection(Number(card.dataset.profile));
      }
    });
  }

  elements.connectBtn.addEventListener("click", async () => {
    try {
      await connectInteractive();
    } catch (error) {
      appendLog(`Connect failed: ${error.message}`);
      setStatus(`Connect failed: ${error.message}`);
      await disconnectClient(`Connect failed: ${error.message}`);
    }
  });

  elements.reconnectBannerBtn.addEventListener("click", async () => {
    elements.transport.value = "hid";
    try {
      await connectInteractive();
    } catch (error) {
      appendLog(`Reconnect failed: ${error.message}`);
      setStatus(`Reconnect failed: ${error.message}`, { showReconnect: true });
    }
  });

  elements.disconnectBtn.addEventListener("click", () => disconnectClient("Disconnected."));

  elements.refreshBtn.addEventListener("click", async () => {
    try {
      await refreshAll();
      setStatus("Refreshed device state.");
    } catch (error) {
      appendLog(`Refresh failed: ${error.message}`);
      setStatus(`Refresh failed: ${error.message}`);
    }
  });

  elements.applyProfileBtn.addEventListener("click", async () => {
    try {
      await handleApplyProfile();
    } catch (error) {
      appendLog(`Profile change failed: ${error.message}`);
      setStatus(`Profile change failed: ${error.message}`);
    }
  });

  elements.discoverBtn.addEventListener("click", async () => {
    try {
      await handleDiscover();
    } catch (error) {
      appendLog(`Discovery failed: ${error.message}`);
      setStatus(`Discovery failed: ${error.message}`);
    }
  });

  elements.blockMacBtn.addEventListener("click", async () => {
    try {
      await handleBlockAddress(elements.blockMacInput.value.trim());
      elements.blockMacInput.value = "";
    } catch (error) {
      appendLog(`Block failed: ${error.message}`);
      setStatus(`Block failed: ${error.message}`);
    }
  });

  elements.applyMicSourceBtn.addEventListener("click", async () => {
    try {
      await handleApplyMicSource();
    } catch (error) {
      appendLog(`Mic source change failed: ${error.message}`);
      setStatus(`Mic source change failed: ${error.message}`);
    }
  });

  elements.refreshDiagBtn.addEventListener("click", () => refreshMetricsAndDiag());

  elements.copyDiagBtn.addEventListener("click", async () => {
    try {
      const json = JSON.stringify(snapshot, null, 2);
      if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(json);
        setStatus("Diagnostics JSON snapshot copied to clipboard.");
        appendLog("Copied diagnostics JSON snapshot.");
      } else {
        appendLog(json);
        setStatus("Clipboard API unavailable; snapshot written to Activity Log.");
      }
    } catch (error) {
      setStatus(`Copy failed: ${error.message}`);
    }
  });

  elements.autoRefreshDiag.addEventListener("change", () => {
    stopAutoRefresh();
    if (elements.autoRefreshDiag.checked && transportClient) {
      diagTimer = setInterval(() => {
        refreshMetricsAndDiag();
      }, 1000);
    }
  });

  elements.otaFile.addEventListener("change", async () => {
    const file = elements.otaFile.files?.[0];
    otaImage = null;
    if (!file) {
      elements.otaSummary.textContent = "Select an XZ OTA package after connecting.";
      syncUiEnabled();
      return;
    }
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      const bodySize = validateOtaImage(bytes);
      otaImage = bytes;
      elements.otaSummary.textContent = `${file.name} (${bytes.length} B total, ${bodySize} B XZ)`;
    } catch (error) {
      elements.otaSummary.textContent = `Invalid package: ${error.message}`;
    }
    syncUiEnabled();
  });

  elements.mfgAck.addEventListener("change", syncUiEnabled);
  elements.uploadBtn.addEventListener("click", handleUploadOta);
  elements.abortBtn.addEventListener("click", handleAbortOta);
  elements.clearLogBtn.addEventListener("click", () => {
    elements.log.textContent = "";
  });

  if ("hid" in navigator) {
    navigator.hid.addEventListener("disconnect", (event) => {
      if (transportClient && connectedKind === "hid" &&
          transportClient.device === event.device && !busy) {
        disconnectClient("USB device disconnected.");
      }
    });
  }
}

wireEvents();
renderAll();
