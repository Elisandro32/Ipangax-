// POST /api/judge  (chamado pelo servidor PHP do juiz, nunca pelo browser)
// Cabeçalho: X-Judge-Token = JUDGE_TOKEN (variável de ambiente do Netlify, 16+ caracteres)
// Corpo:     { mode: "run" | "check", code, input, time_ms, max_steps }
// Resposta:  { verdict: "OK"|"CE"|"RE"|"TLE"|"OLE"|"IE", stdout, stderr, ms }
const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const SRC = path.join(__dirname, "bin", "ipx-linux-x64");
const MAX_OUT = 256 * 1024;

const reply = (status, obj) => ({
  statusCode: status,
  headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  body: JSON.stringify(obj),
});

function tokenOk(given) {
  const want = process.env.JUDGE_TOKEN || "";
  if (want.length < 16 || typeof given !== "string") return false;
  const a = crypto.createHash("sha256").update(given).digest();
  const b = crypto.createHash("sha256").update(want).digest();
  return crypto.timingSafeEqual(a, b);
}

function prepareBinary() {
  const st = fs.statSync(SRC);
  const bin = `/tmp/ipx-${st.size}-${Math.floor(st.mtimeMs)}`;
  if (!fs.existsSync(bin)) {
    fs.copyFileSync(SRC, bin);
    fs.chmodSync(bin, 0o755);
  }
  return bin;
}

function classify(code, err) {
  if (code === 0) return "OK";
  if (code === 2) return "IE";
  if (err.startsWith("Ipangax Error")) return "CE";
  if (err.includes("Execution step limit exceeded")) return "TLE";
  return "RE";
}

exports.handler = async (event) => {
  if (event.httpMethod !== "POST") return reply(405, { error: "Use POST." });
  const hdr = event.headers || {};
  const given = hdr["x-judge-token"] || hdr["X-Judge-Token"];
  if (!process.env.JUDGE_TOKEN || process.env.JUDGE_TOKEN.length < 16)
    return reply(503, { error: "JUDGE_TOKEN não está definido no Netlify (mínimo 16 caracteres)." });
  if (!tokenOk(given)) return reply(401, { error: "Token inválido." });

  let body;
  try {
    const raw = event.isBase64Encoded ? Buffer.from(event.body, "base64").toString() : event.body;
    body = JSON.parse(raw || "{}");
  } catch {
    return reply(400, { error: "JSON inválido." });
  }
  if (typeof body.code !== "string") return reply(400, { error: "Falta 'code'." });
  if (Buffer.byteLength(body.code) > 64000) return reply(413, { error: "Programa demasiado grande." });
  const input = typeof body.input === "string" ? body.input : "";
  if (Buffer.byteLength(input) > 2000000) return reply(413, { error: "Entrada demasiado grande." });
  const mode = body.mode === "check" ? "check" : "run";
  const timeMs = Math.min(8000, Math.max(300, parseInt(body.time_ms, 10) || 5000));
  const steps = Math.min(2000000000, Math.max(1000, parseInt(body.max_steps, 10) || 400000000));

  let bin;
  try {
    bin = prepareBinary();
  } catch {
    return reply(500, { error: "Binário do interpretador em falta no servidor." });
  }
  const file = path.join("/tmp", "j-" + crypto.randomBytes(8).toString("hex") + ".ipa");
  try {
    fs.writeFileSync(file, body.code);
  } catch {
    return reply(500, { error: "Não foi possível preparar o programa." });
  }

  const t0 = Date.now();
  return new Promise((resolve) => {
    const args = [mode, file, "--name", "main.ipa", "--max-steps", String(steps), "--max-array", "1000000"];
    const child = spawn(bin, args, { env: {}, stdio: ["pipe", "pipe", "pipe"] });
    let out = "", err = "", outBytes = 0, killedBy = "";
    const kill = (why) => { if (!killedBy) { killedBy = why; child.kill("SIGKILL"); } };
    const timer = setTimeout(() => kill("TLE"), mode === "check" ? 5000 : timeMs);
    child.stdout.on("data", (d) => {
      outBytes += d.length;
      if (outBytes > MAX_OUT) return kill("OLE");
      out += d;
    });
    child.stderr.on("data", (d) => { if (err.length < 4000) err += d; });
    child.stdin.on("error", () => {});
    child.stdin.end(input);
    const done = (code) => {
      clearTimeout(timer);
      fs.unlink(file, () => {});
      const ms = Date.now() - t0;
      const verdict = killedBy || classify(code, err);
      resolve(reply(200, {
        verdict,
        stdout: verdict === "OK" ? out : out.slice(0, 1000),
        stderr: err.slice(0, 800),
        ms,
      }));
    };
    child.on("error", () => done(2));
    child.on("close", (code) => done(code === null ? 137 : code));
  });
};
