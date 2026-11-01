// POST /api/execute  { code: string, input?: string, mode: "run" | "check" }
// Runs the real Ipangax C++ interpreter (static binary built with `make static`).
// Isolation: each call is a short-lived process inside the Netlify (AWS Lambda) sandbox,
// with a 5 s timeout, 256 KB output cap, 20 KB source and 100 KB input caps, a statement-step
// limit, call-depth, string-size and array-size limits in the interpreter, and an empty environment.
const { execFile } = require("child_process");
const fs = require("fs");
const path = require("path");

const SRC = path.join(__dirname, "bin", "ipx-linux-x64");
const crypto = require("crypto");
const reply = (status, obj) => ({
  statusCode: status,
  headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  body: JSON.stringify(obj),
});

exports.handler = async (event) => {
  if (event.httpMethod !== "POST") return reply(405, { error: "Use POST." });
  let body;
  try {
    const raw = event.isBase64Encoded ? Buffer.from(event.body, "base64").toString() : event.body;
    body = JSON.parse(raw || "{}");
  } catch {
    return reply(400, { error: "Invalid JSON." });
  }
  if (typeof body.code !== "string") return reply(400, { error: "Missing 'code'." });
  if (Buffer.byteLength(body.code) > 20000) return reply(413, { error: "Program too large (limit 20 KB)." });
  const input = typeof body.input === "string" ? body.input : "";
  if (Buffer.byteLength(input) > 100000) return reply(413, { error: "Input too large (limit 100 KB)." });
  const mode = body.mode === "check" ? "check" : "run";

  let BIN;
  try {
    // The copy is named after the binary's size and mtime, so a new build is never shadowed by an old copy.
    const st = fs.statSync(SRC);
    BIN = `/tmp/ipx-${st.size}-${Math.floor(st.mtimeMs)}`;
    if (!fs.existsSync(BIN)) {
      fs.copyFileSync(SRC, BIN);
      fs.chmodSync(BIN, 0o755);
    }
  } catch {
    return reply(500, { error: "Interpreter binary is not available on the server." });
  }

  const file = path.join("/tmp", "p-" + crypto.randomBytes(8).toString("hex") + ".ipa");
  try {
    fs.writeFileSync(file, body.code);
  } catch {
    return reply(500, { error: "Could not prepare the program." });
  }

  return new Promise((resolve) => {
    const child = execFile(
      BIN,
      [mode, file, "--name", "main.ipa", "--max-steps", "5000000", "--max-array", "1000000"],
      { timeout: 5000, maxBuffer: 256 * 1024, env: {} },
      (err, stdout, stderr) => {
        fs.unlink(file, () => {});
        let notice = "";
        if (err && err.code === "ERR_CHILD_PROCESS_STDIO_MAXBUFFER") notice = "Output limit (256 KB) reached; the program was stopped.";
        else if (err && err.killed) notice = "Time limit (5 s) reached; the program was stopped.";
        else if (err && err.signal) notice = "The interpreter stopped unexpectedly (" + err.signal + ").";
        resolve(reply(200, { ok: !err, output: stdout || "", error: stderr || "", notice }));
      }
    );
    child.stdin.on("error", () => {});
    child.stdin.end(input);
  });
};
