const code = document.getElementById("code");
const gut = document.getElementById("gutter");
const out = document.getElementById("out");
const err = document.getElementById("err");
const st = document.getElementById("st");
const btns = document.querySelectorAll(".bar button");

const stdin = document.getElementById("stdin");
code.value = `n = int(input())
nums = split(input())

total = 0
for word in nums {
    total = total + int(word)
}

print("n = " + string(n))
print("sum = " + string(total))
`;
stdin.value = "3\n10 20 30\n";

function lines() {
  const n = code.value.split("\n").length;
  gut.textContent = Array.from({ length: n }, (_, i) => i + 1).join("\n");
}
code.addEventListener("input", lines);
code.addEventListener("scroll", () => { gut.scrollTop = code.scrollTop; });
code.addEventListener("keydown", (e) => {
  if (e.key === "Tab") {
    e.preventDefault();
    code.setRangeText("    ", code.selectionStart, code.selectionEnd, "end");
    lines();
  } else if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    send("run");
  }
});

async function send(mode) {
  btns.forEach((b) => (b.disabled = true));
  st.textContent = mode === "run" ? "Running..." : "Checking...";
  out.textContent = "";
  err.textContent = "";
  try {
    const r = await fetch("/api/execute", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code: code.value, input: stdin.value, mode }),
    });
    const d = await r.json();
    if (!r.ok) {
      err.textContent = d.error || "Server error " + r.status;
      st.textContent = "Failed";
    } else {
      out.textContent = d.output;
      err.textContent = (d.error + (d.notice ? "\n" + d.notice : "")).trim();
      st.textContent = d.ok ? "Finished" : "Finished with errors";
    }
  } catch {
    err.textContent =
      "Could not reach /api/execute. The playground needs the server function: " +
      "deploy to Netlify, or run 'netlify dev' locally. Opening the file directly does not work.";
    st.textContent = "Not connected";
  }
  btns.forEach((b) => (b.disabled = false));
}

document.getElementById("run").onclick = () => send("run");
document.getElementById("check").onclick = () => send("check");
document.getElementById("clear").onclick = () => { out.textContent = ""; err.textContent = ""; st.textContent = ""; };
lines();
