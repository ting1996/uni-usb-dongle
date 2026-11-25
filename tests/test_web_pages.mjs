import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { pathToFileURL } from "node:url";
import { buildWebPages } from "../tools/build_web_pages.mjs";

test("buildWebPages stages static GitHub Pages bundle with .js modules and .nojekyll", () => {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "uni-usb-dongle-pages-"));
  try {
    const result = buildWebPages({ outDir });
    assert.deepEqual(result.files, [
      ".nojekyll",
      "app.js",
      "hid-protocol.js",
      "index.html",
      "protocol.js",
      "styles.css",
    ]);

    const indexHtml = fs.readFileSync(path.join(outDir, "index.html"), "utf8");
    assert.match(indexHtml, /href="\.\/styles\.css"/);
    assert.match(indexHtml, /src="\.\/app\.js"/);
    assert.match(indexHtml, /data-theme="black"/);
    assert.match(indexHtml, /id="themeToggleBtn"/);
    assert.doesNotMatch(indexHtml, /\.mjs/);

    const stylesCss = fs.readFileSync(path.join(outDir, "styles.css"), "utf8");
    assert.match(stylesCss, /:root\[data-theme="black"\]/);
    assert.match(stylesCss, /--bg:\s*#000000/);

    const appJs = fs.readFileSync(path.join(outDir, "app.js"), "utf8");
    assert.match(appJs, /from "\.\/protocol\.js"/);
    assert.match(appJs, /from "\.\/hid-protocol\.js"/);
    assert.doesNotMatch(appJs, /\.mjs/);

    const hidJs = fs.readFileSync(path.join(outDir, "hid-protocol.js"), "utf8");
    assert.match(hidJs, /from "\.\/protocol\.js"/);
    assert.doesNotMatch(hidJs, /\.mjs/);
  } finally {
    fs.rmSync(outDir, { recursive: true, force: true });
  }
});

test("staged .js modules parse and execute cleanly as ES modules", async () => {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "uni-usb-dongle-pages-"));
  try {
    buildWebPages({ outDir });
    fs.writeFileSync(
      path.join(outDir, "package.json"),
      '{ "type": "module" }\n',
      "utf8",
    );

    execFileSync(process.execPath, ["--check", path.join(outDir, "app.js")]);

    const protocolMod = await import(pathToFileURL(path.join(outDir, "protocol.js")).href);
    const hidMod = await import(pathToFileURL(path.join(outDir, "hid-protocol.js")).href);

    const payload = Uint8Array.of(1, 2, 3, 4);
    const packet = protocolMod.encodeFrame(protocolMod.COMMAND.getDeviceState, 42, payload);
    const decoded = protocolMod.decodeFrame(packet);
    assert.equal(decoded.command, protocolMod.COMMAND.getDeviceState);
    assert.equal(decoded.sequence, 42);
    assert.deepEqual(decoded.payload, payload);
    assert.equal(hidMod.HID_FILTERS.length, 5);
  } finally {
    fs.rmSync(outDir, { recursive: true, force: true });
  }
});
