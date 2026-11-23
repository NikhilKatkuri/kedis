#!/usr/bin/env node

const net = require("node:net");
const readline = require("node:readline");

const DEFAULT_PORT = 7379;
const CONNECT_TIMEOUT_MS = 5000;

const defaultConfig = {
  host: process.env.KEDIS_HOST || "127.0.0.1",
  port: Number(process.env.KEDIS_PORT || DEFAULT_PORT),
  prompt: "kedis> ",
};

const commands = [
  { name: "PING", usage: "PING", description: "Check server connection" },
  { name: "SET", usage: "SET key val [-ttl=ms]", description: "Set key value (optional TTL in ms)" },
  { name: "GET", usage: "GET key", description: "Get value for key" },
  { name: "DEL", usage: "DEL key [key ...]", description: "Delete one or more keys" },
  { name: "EXISTS", usage: "EXISTS key", description: "Check if a key exists" },
  { name: "PERSIST", usage: "PERSIST key", description: "Remove TTL from key" },
  { name: "MSET", usage: "MSET k1 v1 [k2 v2 ...]", description: "Set multiple key-value pairs" },
  { name: "MGET", usage: "MGET k1 [k2 ...]", description: "Get multiple keys" },
  { name: "SETNX", usage: "SETNX key val", description: "Set key only if it does not exist" },
  { name: "COUNT", usage: "COUNT", description: "Return total key count" },
  { name: "STATS", usage: "STATS", description: "Return server stats" },
  { name: "FLUSHDB", usage: "FLUSHDB", description: "Remove all keys" },
  { name: "HELP", usage: "HELP", description: "Show this help message" },
  { name: "CONNECT", usage: "CONNECT host[:port] | url [port]", description: "Switch to a different server (port defaults to 7379)" },
  { name: "exit / quit", usage: "exit / quit", description: "Close shell" },
];

let currentSocket = null;
let rl = null;
let intentionalClose = false;
let recvBuffer = "";

function clearPromptLine() {
  if (!rl) return;
  readline.clearLine(process.stdout, 0);
  readline.cursorTo(process.stdout, 0);
}

function showPrompt() {
  if (!rl || intentionalClose) return;
  rl.prompt();
}

function tryPrintJsonTable(rawLine) {
  let text = rawLine.trim();
  if (text.startsWith("+")) {
    text = text.slice(1).trim();
  }

  if (!text.startsWith("{") && !text.startsWith("[")) {
    return false;
  }

  try {
    const parsed = JSON.parse(text);
    if (parsed && typeof parsed === "object") {
      console.table(parsed);
      return true;
    }
  } catch {
    // Not valid JSON; print as regular text
  }

  return false;
}

function renderMessage(msg) {
  const cleaned = msg.replace(/\r$/, "");
  if (!cleaned) return;

  clearPromptLine();
  if (!tryPrintJsonTable(cleaned)) {
    process.stdout.write(cleaned + "\n");
  }
  showPrompt();
}

function onData(chunk) {
  recvBuffer += chunk.toString("utf8");

  let newlineIdx;
  while ((newlineIdx = recvBuffer.indexOf("\n")) !== -1) {
    const line = recvBuffer.slice(0, newlineIdx);
    recvBuffer = recvBuffer.slice(newlineIdx + 1);
    renderMessage(line);
  }
}

function flushRemainingBuffer() {
  if (recvBuffer.trim().length > 0) {
    const leftover = recvBuffer;
    recvBuffer = "";
    renderMessage(leftover);
  } else {
    recvBuffer = "";
  }
}

/**
 * Turns whatever the user typed after "connect" into { host, port, scheme }.
 * Accepts: host, host:port, host port, [::1]:7379, and URLs such as
 * http://host/, https://host:7379/path (scheme, credentials, path, query and
 * fragment are ignored). The port defaults to 7379.
 * Returns null if the input cannot be understood.
 */
function parseTarget(args) {
  if (!args.length || !args[0]) return null;

  let raw = args[0];
  let scheme = null;

  const schemeMatch = raw.match(/^([a-z][a-z0-9+.-]*):\/\//i);
  if (schemeMatch) {
    scheme = schemeMatch[1].toLowerCase();
    raw = raw.slice(schemeMatch[0].length);
  }

  raw = raw.split(/[/?#]/)[0]; // drop path, query, fragment

  const at = raw.lastIndexOf("@"); // drop user:password@
  if (at !== -1) raw = raw.slice(at + 1);

  let host = raw;
  let port = null;

  if (raw.startsWith("[")) {
    // IPv6 literal: [::1] or [::1]:7379
    const end = raw.indexOf("]");
    if (end === -1) return null;
    host = raw.slice(1, end);
    const rest = raw.slice(end + 1);
    if (rest.startsWith(":")) port = rest.slice(1);
  } else {
    const colon = raw.lastIndexOf(":");
    // exactly one colon means host:port (more than one is a bare IPv6 address)
    if (colon !== -1 && raw.indexOf(":") === colon) {
      host = raw.slice(0, colon);
      port = raw.slice(colon + 1);
    }
  }

  if (args[1] !== undefined) port = args[1]; // "connect host 7379" wins
  if (port === null || port === "") port = DEFAULT_PORT;

  const parsedPort = Number(port);
  if (!host || !Number.isInteger(parsedPort) || parsedPort <= 0 || parsedPort > 65535) {
    return null;
  }

  return { host, port: parsedPort, scheme };
}

function connectToArgs(args) {
  const target = parseTarget(args);

  if (!target) {
    clearPromptLine();
    console.log("Usage: connect <host> [port]   (also accepts host:port or a URL)");
    showPrompt();
    return;
  }

  const looksLikeWebUrl = ["http", "https", "ws", "wss"].includes(target.scheme);
  const isDevTunnel = /\.devtunnels\.ms$/i.test(target.host);

  if (looksLikeWebUrl || isDevTunnel) {
    clearPromptLine();
    console.log(
      "Note: that looks like a web (HTTP) address. kedis speaks raw TCP, so web\n" +
        "relays such as devtunnels.ms usually cannot carry it. If the connection\n" +
        "fails, forward the port to this machine (for example with\n" +
        "`devtunnel connect <tunnel-id>`) and use: connect 127.0.0.1 " + DEFAULT_PORT
    );
  }

  connectTo(target.host, target.port);
}

function connectTo(host, port) {
  const parsedPort = Number(port);
  if (!host || !Number.isInteger(parsedPort) || parsedPort <= 0 || parsedPort > 65535) {
    clearPromptLine();
    console.log("Usage: connect <host> [port] (e.g., connect 127.0.0.1 7380)");
    showPrompt();
    return;
  }

  if (currentSocket) {
    const prevSocket = currentSocket;
    currentSocket = null;
    recvBuffer = "";
    prevSocket.removeAllListeners();
    prevSocket.on("error", () => {});
    prevSocket.end();
    prevSocket.destroy();
  }

  intentionalClose = false;
  recvBuffer = "";

  const nextSocket = net.createConnection({ host, port: parsedPort });
  currentSocket = nextSocket;

  // Give up if the server does not answer, instead of hanging forever.
  nextSocket.setTimeout(CONNECT_TIMEOUT_MS);
  nextSocket.on("timeout", () => {
    if (currentSocket !== nextSocket) return;
    clearPromptLine();
    console.error(`Connection to ${host}:${parsedPort} timed out.`);
    nextSocket.destroy();
  });

  nextSocket.on("connect", () => {
    if (currentSocket !== nextSocket) return;
    nextSocket.setTimeout(0); // connected: no idle timeout
    defaultConfig.host = host;
    defaultConfig.port = parsedPort;
    clearPromptLine();
    console.log(`Connected to kedis at ${host}:${parsedPort}.`);
    showPrompt();
  });

  nextSocket.on("data", (chunk) => {
    if (currentSocket !== nextSocket) return;
    onData(chunk);
  });

  nextSocket.on("error", (err) => {
    if (currentSocket !== nextSocket) return;
    clearPromptLine();
    console.error(`Connection error (${host}:${parsedPort}): ${err.message}`);
    showPrompt();
  });

  nextSocket.on("close", () => {
    if (currentSocket !== nextSocket) return;
    flushRemainingBuffer();
    currentSocket = null;

    if (intentionalClose) {
      if (rl) rl.close();
      console.log("\nDisconnected from kedis.");
    } else {
      clearPromptLine();
      console.log(`Connection to ${host}:${parsedPort} closed.`);
      showPrompt();
    }
  });
}

function handleLine(line) {
  const trimmed = line.trim();
  if (!trimmed) {
    showPrompt();
    return;
  }

  const parts = trimmed.split(/\s+/);
  const cmd = parts[0].toLowerCase();

  if (cmd === "help") {
    clearPromptLine();
    console.table(commands);
    showPrompt();
    return;
  }

  if (cmd === "connect") {
    connectToArgs(parts.slice(1));
    return;
  }

  if (cmd === "exit" || cmd === "quit") {
    intentionalClose = true;
    if (rl) rl.close();
    if (currentSocket && !currentSocket.destroyed) {
      currentSocket.end();
    } else {
      console.log("\nDisconnected from kedis.");
    }
    return;
  }

  if (!currentSocket || currentSocket.destroyed || !currentSocket.writable) {
    clearPromptLine();
    console.log('Not connected. Use "connect <host> [port]" or "exit".');
    showPrompt();
    return;
  }

  currentSocket.write(trimmed + "\n");
}

function main() {
  rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    prompt: defaultConfig.prompt,
  });

  rl.on("line", handleLine);

  rl.on("close", () => {
    if (!intentionalClose) {
      intentionalClose = true;
      if (currentSocket && !currentSocket.destroyed) {
        currentSocket.end();
      }
    }
  });

  console.log(`Connecting to kedis at ${defaultConfig.host}:${defaultConfig.port}...`);
  connectTo(defaultConfig.host, defaultConfig.port);
}

main();