import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SCRIPT_DIR, "..");
const DEFAULT_SOURCE_DIR = path.resolve(REPO_ROOT, "web");
const DEFAULT_OUT_DIR = path.resolve(REPO_ROOT, "build", "pages");

const HTML_REF_RE = /(?:src|href)\s*=\s*["'](\.\.?\/[^"']+)["']/g;
const JS_REF_RE = /(?:from|import)\s*\(?\s*["'](\.\.?\/[^"']+)["']/g;

function isSamePath(left, right) {
  const a = path.resolve(left);
  const b = path.resolve(right);
  return process.platform === "win32"
    ? a.toLowerCase() === b.toLowerCase()
    : a === b;
}

function isInsideDir(parentDir, targetPath) {
  const relative = path.relative(parentDir, targetPath);
  return relative !== "" && !relative.startsWith("..") && !path.isAbsolute(relative);
}

function walkFiles(rootDir, currentDir = rootDir, list = []) {
  const entries = fs.readdirSync(currentDir, { withFileTypes: true });
  entries.sort((a, b) => a.name.localeCompare(b.name));
  for (const entry of entries) {
    const fullPath = path.join(currentDir, entry.name);
    if (entry.isDirectory()) {
      walkFiles(rootDir, fullPath, list);
    } else if (entry.isFile()) {
      list.push(path.relative(rootDir, fullPath));
    }
  }
  return list;
}

function rewriteHtmlRefs(content) {
  return content.replace(
    /((?:src|href)\s*=\s*["']\.\.?\/[^"']*?)\.mjs(["'])/g,
    "$1.js$2",
  );
}

function rewriteJsRefs(content) {
  return content.replace(
    /((?:from|import)\s*\(?\s*["']\.\.?\/[^"']*?)\.mjs(["']\s*\)?)/g,
    "$1.js$2",
  );
}

function validateReferences(filePath, outDir, regex) {
  const content = fs.readFileSync(filePath, "utf8");
  const baseDir = path.dirname(filePath);
  for (const match of content.matchAll(regex)) {
    const rawRef = match[1];
    const cleanRef = rawRef.split(/[?#]/, 1)[0];
    if (cleanRef.endsWith(".mjs")) {
      throw new Error(`Unrewritten .mjs reference "${rawRef}" in ${filePath}`);
    }
    const targetPath = path.resolve(baseDir, cleanRef);
    if (!isInsideDir(outDir, targetPath)) {
      throw new Error(`Reference "${rawRef}" in ${filePath} escapes output directory`);
    }
    if (!fs.existsSync(targetPath) || !fs.statSync(targetPath).isFile()) {
      throw new Error(`Missing referenced file "${rawRef}" from ${filePath}`);
    }
  }
}

/**
 * Stages a GitHub Pages-ready static bundle from sourceDir into outDir.
 * Renames .mjs modules to .js, updates relative HTML/JS references, and adds .nojekyll.
 */
export function buildWebPages({
  sourceDir = DEFAULT_SOURCE_DIR,
  outDir = DEFAULT_OUT_DIR,
} = {}) {
  const resolvedSource = path.resolve(sourceDir);
  const resolvedOut = path.resolve(outDir);
  const fsRoot = path.parse(resolvedOut).root;

  if (
    isSamePath(resolvedOut, resolvedSource) ||
    isSamePath(resolvedOut, REPO_ROOT) ||
    isSamePath(resolvedOut, fsRoot)
  ) {
    throw new Error(`Refusing unsafe output directory: ${resolvedOut}`);
  }

  if (!fs.existsSync(resolvedSource) || !fs.statSync(resolvedSource).isDirectory()) {
    throw new Error(`Source directory does not exist: ${resolvedSource}`);
  }

  fs.rmSync(resolvedOut, { recursive: true, force: true });
  fs.mkdirSync(resolvedOut, { recursive: true });

  for (const relPath of walkFiles(resolvedSource)) {
    const srcPath = path.join(resolvedSource, relPath);
    const destRel = relPath.endsWith(".mjs")
      ? `${relPath.slice(0, -4)}.js`
      : relPath;
    const destPath = path.join(resolvedOut, destRel);

    fs.mkdirSync(path.dirname(destPath), { recursive: true });

    if (destPath.endsWith(".html")) {
      const content = fs.readFileSync(srcPath, "utf8");
      fs.writeFileSync(destPath, rewriteHtmlRefs(content), "utf8");
    } else if (destPath.endsWith(".js")) {
      const content = fs.readFileSync(srcPath, "utf8");
      fs.writeFileSync(destPath, rewriteJsRefs(content), "utf8");
    } else {
      fs.copyFileSync(srcPath, destPath);
    }
  }

  fs.writeFileSync(path.join(resolvedOut, ".nojekyll"), "", "utf8");

  const stagedFiles = walkFiles(resolvedOut)
    .map((rel) => rel.split(path.sep).join("/"))
    .sort();

  if (!stagedFiles.includes("index.html") || !stagedFiles.includes(".nojekyll")) {
    throw new Error("Staged bundle is missing required index.html or .nojekyll");
  }

  for (const relPosix of stagedFiles) {
    if (relPosix.endsWith(".mjs")) {
      throw new Error(`Unexpected .mjs file in staged bundle: ${relPosix}`);
    }
    const fullPath = path.join(resolvedOut, relPosix);
    if (relPosix.endsWith(".html")) {
      validateReferences(fullPath, resolvedOut, HTML_REF_RE);
    } else if (relPosix.endsWith(".js")) {
      validateReferences(fullPath, resolvedOut, JS_REF_RE);
    }
  }

  return { outDir: resolvedOut, files: stagedFiles };
}

function parseArgs(argv) {
  const options = {};
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--out" || arg === "--source") {
      const value = argv[i + 1];
      if (!value || value.startsWith("--")) {
        throw new Error(`Missing value for ${arg}`);
      }
      if (arg === "--out") {
        options.outDir = path.resolve(value);
      } else {
        options.sourceDir = path.resolve(value);
      }
      i += 1;
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  return options;
}

const isMain = process.argv[1] &&
  path.resolve(process.argv[1]).toLowerCase() ===
    path.resolve(fileURLToPath(import.meta.url)).toLowerCase();

if (isMain) {
  const result = buildWebPages(parseArgs(process.argv.slice(2)));
  console.log(`Staged ${result.files.length} files in ${result.outDir}: ${result.files.join(", ")}`);
}
