#include <pthread.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../compiler/ipangax.hpp"

static const char* HELP =
    "Ipangax command line tool\n\n"
    "Usage: ipx <command> [file.ipa]\n\n"
    "Commands:\n"
    "  run <file|->   Check and run a program (- reads standard input)\n"
    "  check <file>   Check a program without running it\n"
    "  version        Print the version\n"
    "\nOptions:\n  --max-steps N   stop after N executed statements\n  --max-array N   limit array length\n  --name NAME     file name shown in messages\n\n"
    "  help           Show this message\n\n"
    "Not implemented yet: build, fmt, repl\n";

static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static void report(const std::vector<ipx::Diagnostic>& diags, const std::string& file,
                   const std::string& src) {
  std::cout.flush();
  for (auto& d : diags) std::cerr << ipx::formatDiagnostic(d, file, src) << "\n";
  std::cerr << diags.size() << (diags.size() == 1 ? " error" : " errors") << "\n";
}

static int realMain(int argc, char** argv) {
  std::ios::sync_with_stdio(false);
  std::string cmd = argc > 1 ? argv[1] : "help";
  if (cmd == "help" || cmd == "--help" || cmd == "-h") { std::cout << HELP; return 0; }
  if (cmd == "version" || cmd == "--version") { std::cout << "Ipangax " << ipx::VERSION << "\n"; return 0; }
  if (cmd == "build" || cmd == "fmt" || cmd == "repl") {
    std::cerr << "ipx " << cmd << " is not implemented yet.\n";
    return 2;
  }
  if (cmd != "run" && cmd != "check") {
    std::cerr << "Unknown command '" << cmd << "'. Run 'ipx help'.\n";
    return 2;
  }
  std::string file, src;
  unsigned long long maxSteps = 0, maxArray = 10000000;
  std::string shownName;
  for (int k = 2; k < argc; ++k) {
    std::string a = argv[k];
    if (a == "--max-steps" && k + 1 < argc) maxSteps = std::strtoull(argv[++k], nullptr, 10);
    else if (a == "--max-array" && k + 1 < argc) maxArray = std::strtoull(argv[++k], nullptr, 10);
    else if (a == "--name" && k + 1 < argc) shownName = argv[++k];
    else file = a;
  }
  if (file.empty()) {
    std::cerr << "Usage: ipx " << cmd << " <file.ipa | ->\n";
    return 2;
  }
  if (file == "-") {  // read the program from standard input (used by the web playground)
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    src = ss.str();
    file = "main.ipa";
  } else if (!readFile(file, src)) {
    std::cerr << "Cannot read file '" << file << "'.\n";
    return 2;
  }
  if (!shownName.empty()) file = shownName;
  std::vector<ipx::Diagnostic> diags;
  if (cmd == "check") {
    diags = ipx::check(src);
    if (diags.empty()) { std::cout << file << ": no errors found\n"; return 0; }
    report(diags, file, src);
    return 1;
  }
  ipx::RunOptions opts;
  opts.maxSteps = maxSteps;
  opts.maxArray = maxArray;
  opts.in = &std::cin;
  bool ok = ipx::run(src, std::cout, diags, opts);
  if (!ok) { report(diags, file, src); return 1; }
  return 0;
}

// The interpreter is recursive, so it runs on a thread with a large stack (virtual memory only;
// it is not committed until used). Deeply nested programs then fail with a clear error, not a crash.
struct MainArgs {
  int argc;
  char** argv;
  int rc;
};

static void* trampoline(void* p) {
  MainArgs* a = static_cast<MainArgs*>(p);
  a->rc = realMain(a->argc, a->argv);
  return nullptr;
}

int main(int argc, char** argv) {
#ifdef IPX_NO_THREAD
  return realMain(argc, argv);
#endif
  MainArgs a{argc, argv, 0};
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, (size_t)256 << 20);
  pthread_t t;
  if (pthread_create(&t, &attr, trampoline, &a) != 0) return realMain(argc, argv);
  pthread_join(t, nullptr);
  return a.rc;
}
