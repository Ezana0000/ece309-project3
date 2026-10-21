// src/tools/workspace.cpp — Workspace::resolve (spec §4.4).
//
// A text-only check that keeps a model-supplied path inside the workspace.
// It never touches the filesystem; check_symlinks() handles links afterwards.

#include "tools/workspace.h"

namespace fs = std::filesystem;

bool Workspace::resolve(std::string_view user_path, fs::path* out,
                        std::string* reason) const {
    // Step 1: nothing to resolve.
    if (user_path.empty()) {
        *reason = "empty path";
        return false;
    }

    // Step 2: the OS would cut the path off at a NUL byte, so refuse it.
    if (user_path.find('\0') != std::string_view::npos) {
        *reason = "path contains a NUL byte";
        return false;
    }

    // Step 3: only paths relative to the workspace are allowed.
    fs::path p{std::string(user_path)};
    if (p.is_absolute() || p.has_root_path()) {
        *reason = "absolute paths are not allowed: " + std::string(user_path);
        return false;
    }

    // Step 4: clean up ".", "//" and "x/.." without touching the disk.
    fs::path n = p.lexically_normal();

    // Step 5: a leading ".." means the path climbs above the workspace.
    if (!n.empty() && *n.begin() == "..") {
        *reason = "path escapes the workspace: " + std::string(user_path);
        return false;
    }

    // Step 6: "." is the workspace itself; anything else goes under it.
    if (n.empty() || n == ".") {
        *out = root();
    } else {
        *out = root() / n;
    }
    return true;
}
