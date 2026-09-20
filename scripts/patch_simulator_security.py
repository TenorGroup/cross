"""Align the pinned native adapter with production network APIs."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


MIDDLEWARE_PATCH_NAME = "simulator-web-middleware"
MIDDLEWARE_REPLACEMENTS = (
    (
        "src/WebServer.h",
        """class WebServer;

class RequestHandler {
""",
        """class WebServer;

class Middleware {
public:
  using Callback = std::function<bool(void)>;
  using Function = std::function<bool(WebServer &server, Callback next)>;
};

class RequestHandler {
""",
    ),
    (
        "src/WebServer.h",
        """  void handleClient();
  void enableCORS(bool enabled);

  void on(const char *uri, int method, std::function<void()> handler);
""",
        """  void handleClient();
  void enableCORS(bool enabled);
  WebServer &addMiddleware(Middleware::Function fn);

  void on(const char *uri, int method, std::function<void()> handler);
""",
    ),
    (
        "src/WebServer.cpp",
        """  std::vector<Route> routes;
  std::vector<std::unique_ptr<RequestHandler>> requestHandlers;
""",
        """  std::vector<Route> routes;
  std::vector<Middleware::Function> middlewares;
  std::vector<std::unique_ptr<RequestHandler>> requestHandlers;
""",
    ),
    (
        "src/WebServer.cpp",
        """  std::vector<RequestHandler *> rawHandlers(WebServer &server) {
""",
        """  bool runMiddlewares(WebServer &server, Middleware::Callback finalizer) {
    std::function<bool(size_t)> run;
    run = [&](size_t index) {
      if (index == middlewares.size())
        return finalizer();
      return middlewares[index](server, [&] { return run(index + 1); });
    };
    return run(0);
  }

  std::vector<RequestHandler *> rawHandlers(WebServer &server) {
""",
    ),
    (
        "src/WebServer.cpp",
        """      bool handled = false;
      for (const auto &route : impl_->routes) {
""",
        """      const bool middlewareHandled = impl_->runMiddlewares(*this, [this, &contentType, &body] {
        bool handled = false;
      for (const auto &route : impl_->routes) {
""",
    ),
    (
        "src/WebServer.cpp",
        """      if (!handled) {
        if (impl_->notFoundHandler) {
          impl_->notFoundHandler();
        } else {
          send(404, "text/plain", "Not Found");
        }
      }

      ::close(client);
""",
        """      if (!handled) {
        if (impl_->notFoundHandler) {
          impl_->notFoundHandler();
        } else {
          send(404, "text/plain", "Not Found");
        }
        handled = true;
      }
        return handled;
      });
      (void)middlewareHandled;

      ::close(client);
""",
    ),
    (
        "src/WebServer.cpp",
        """void WebServer::handleClient() {}

void WebServer::on(const char *uri, int method, std::function<void()> handler) {
""",
        """void WebServer::handleClient() {}

WebServer &WebServer::addMiddleware(Middleware::Function fn) {
  impl_->middlewares.push_back(std::move(fn));
  return *this;
}

void WebServer::on(const char *uri, int method, std::function<void()> handler) {
""",
    ),
)


def align_middleware(root: Path) -> str:
    pending = []
    aligned = []
    for relative, before, after in MIDDLEWARE_REPLACEMENTS:
        path = root / relative
        text = path.read_text()
        before_count = text.count(before)
        after_count = text.count(after)
        if after_count == 1:
            aligned.append(path)
        elif before_count == 1:
            pending.append((path, before, after))
        else:
            raise RuntimeError(
                f"{MIDDLEWARE_PATCH_NAME} hunk does not match {relative}; inspect upstream changes"
            )

    if aligned and pending:
        raise RuntimeError(f"{MIDDLEWARE_PATCH_NAME} is only partially applied")
    if aligned:
        return "already aligned"

    updated = {}
    for path, before, after in pending:
        text = updated.get(path, path.read_text())
        if text.count(before) != 1 or after in text:
            raise RuntimeError(f"{MIDDLEWARE_PATCH_NAME} preflight changed for {path}")
        updated[path] = text.replace(before, after, 1)
    for path, text in updated.items():
        path.write_text(text)
    return "aligned"


def middleware_is_aligned(root: Path) -> bool:
    return all((root / relative).read_text().count(after) == 1
               for relative, _, after in MIDDLEWARE_REPLACEMENTS)


def reverse_check_without_middleware(root: Path, patch: Path) -> bool:
    with tempfile.TemporaryDirectory(prefix="simulator-patch-check-") as temporary:
        clean_root = Path(temporary) / "simulator"
        shutil.copytree(root, clean_root)
        updated = {}
        for relative, before, after in MIDDLEWARE_REPLACEMENTS:
            path = clean_root / relative
            text = updated.get(path, path.read_text())
            if text.count(after) != 1:
                return False
            updated[path] = text.replace(after, before, 1)
        for path, text in updated.items():
            path.write_text(text)
        return subprocess.run(
            ["git", "apply", "--reverse", "--check", str(patch)],
            cwd=clean_root,
            capture_output=True,
            text=True,
        ).returncode == 0


def align_file_patches(project: Path, root: Path) -> None:
    for name in ("simulator-security.patch", "simulator-http-polling.patch"):
        patch = project / "scripts/patches" / name

        def check(*flags):
            return subprocess.run(
                ["git", "apply", *flags, str(patch)],
                cwd=root,
                capture_output=True,
                text=True,
            )

        if check("--reverse", "--check").returncode == 0:
            print(f"Simulator adapter already aligned: {name}")
        elif middleware_is_aligned(root) and reverse_check_without_middleware(root, patch):
            print(f"Simulator adapter already aligned: {name}")
        elif check("--check").returncode == 0:
            subprocess.run(["git", "apply", str(patch)], cwd=root, check=True)
            print(f"Aligned simulator adapter: {name}")
        else:
            raise RuntimeError(f"{name} does not match the pinned dependency; inspect upstream changes")


def cli() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator-root", type=Path, required=True)
    parser.add_argument("--middleware-only", action="store_true")
    args = parser.parse_args()
    root = args.simulator_root.resolve()
    if not args.middleware_only:
        project = Path(__file__).resolve().parents[1]
        align_file_patches(project, root)
    state = align_middleware(root)
    print(f"Simulator adapter {state}: {MIDDLEWARE_PATCH_NAME}")
    return 0


if "Import" in globals():
    Import("env")
    project = Path(env["PROJECT_DIR"])
    root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator"
    # The native polling adapter uses system libcurl; firmware keeps its own TLS stack.
    env.AppendUnique(LIBS=["curl"])
    if root.exists():
        align_file_patches(project, root)
        state = align_middleware(root)
        print(f"Simulator adapter {state}: {MIDDLEWARE_PATCH_NAME}")
elif __name__ == "__main__":
    raise SystemExit(cli())
