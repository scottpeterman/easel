#!/usr/bin/env bash
# Build, test and package Easel on Linux and macOS.
#
#   scripts/build.sh                  configure (if needed), build, test
#   scripts/build.sh --run            ...then launch the app
#   scripts/build.sh --appimage       ...then make dist/Easel-linux-x86_64.AppImage (Linux)
#   scripts/build.sh --dmg            ...then make dist/Easel-macos.dmg (macOS)
#   scripts/build.sh --clean          wipe the build directory first
#   scripts/build.sh --qt DIR         Qt prefix, e.g. ~/Qt/6.10.3/gcc_64
#   scripts/build.sh --debug          Debug build in build-debug/
#   scripts/build.sh --no-tests       skip ctest
#
# Qt is taken from --qt, then $QT_ROOT_DIR (set by CI), then ~/Qt/<version>/<arch>.
# A build directory configured from another source tree, another Qt or another
# generator is wiped automatically.

set -euo pipefail

QT_VERSION_DEFAULT="6.10.3"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"

qt_dir=""
build_type="Release"
clean=0
run_tests=1
make_appimage=0
make_dmg=0
run_app=0

usage() { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --qt) qt_dir="$2"; shift 2 ;;
        --qt=*) qt_dir="${1#*=}"; shift ;;
        --clean) clean=1; shift ;;
        --debug) build_type="Debug"; shift ;;
        --no-tests) run_tests=0; shift ;;
        --appimage) make_appimage=1; shift ;;
        --dmg) make_dmg=1; shift ;;
        --run) run_app=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

os="$(uname -s)"
case "$os" in
    Linux) qt_arch="gcc_64"; qt_os="linux" ;;
    Darwin) qt_arch="macos"; qt_os="mac" ;;
    *) echo "Unsupported OS: $os (use scripts/build-windows.bat on Windows)" >&2; exit 1 ;;
esac

if [[ $make_appimage -eq 1 && "$os" != "Linux" ]]; then
    echo "--appimage is Linux only" >&2; exit 2
fi
if [[ $make_dmg -eq 1 && "$os" != "Darwin" ]]; then
    echo "--dmg is macOS only" >&2; exit 2
fi

log() { printf '\n==> %s\n' "$*"; }

# --- Qt -----------------------------------------------------------------------

if [[ -z "$qt_dir" ]]; then
    if [[ -n "${QT_ROOT_DIR:-}" ]]; then
        qt_dir="$QT_ROOT_DIR"
    else
        qt_dir="$HOME/Qt/$QT_VERSION_DEFAULT/$qt_arch"
    fi
fi
qt_dir="$(cd "$qt_dir" 2>/dev/null && pwd -P)" || { echo "Qt not found at ${qt_dir}. Pass --qt DIR." >&2; exit 1; }

if [[ ! -f "$qt_dir/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "$qt_dir is not a Qt 6 prefix (no lib/cmake/Qt6)." >&2
    exit 1
fi
if [[ ! -d "$qt_dir/lib/cmake/Qt6ShaderTools" ]]; then
    echo "Qt at $qt_dir has no Shader Tools module. Add it with:" >&2
    echo "  aqt install-qt $qt_os desktop <version> $qt_arch --noarchives -m qtshadertools -O ~/Qt" >&2
    exit 1
fi

# --- Build directory ----------------------------------------------------------

if [[ "$build_type" == "Debug" ]]; then
    build_dir="$ROOT/build-debug"
else
    build_dir="$ROOT/build"
fi
cache="$build_dir/CMakeCache.txt"

cache_value() {
    # Prints the value of a CMakeCache entry, or nothing.
    grep -E "^$1:[A-Z]+=" "$cache" 2>/dev/null | head -n1 | cut -d= -f2- || true
}

if [[ $clean -eq 0 && -f "$cache" ]]; then
    reason=""
    cached_src="$(cache_value CMAKE_HOME_DIRECTORY)"
    cached_qt="$(cache_value CMAKE_PREFIX_PATH)"
    cached_gen="$(cache_value CMAKE_GENERATOR)"
    if [[ -n "$cached_src" && "$(cd "$cached_src" 2>/dev/null && pwd -P || echo "$cached_src")" != "$ROOT" ]]; then
        reason="it was configured from $cached_src"
    elif [[ "$cached_qt" != "$qt_dir" ]]; then
        reason="it was configured for Qt at ${cached_qt:-<unknown>}"
    elif [[ "$cached_gen" != "Ninja" ]]; then
        reason="it uses the ${cached_gen:-<unknown>} generator"
    fi
    if [[ -n "$reason" ]]; then
        log "Wiping $build_dir: $reason"
        clean=1
    fi
fi
if [[ $clean -eq 1 ]]; then
    rm -rf "$build_dir"
fi

# --- Configure, build, test ---------------------------------------------------

log "Configuring ($build_type, Qt $qt_dir)"
cmake -S "$ROOT" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$build_type" -DCMAKE_PREFIX_PATH="$qt_dir"

log "Building"
cmake --build "$build_dir" --parallel

if [[ $run_tests -eq 1 ]]; then
    log "Testing"
    # GPU tests need a display. Headless Linux (CI, SSH) gets one from Xvfb.
    if [[ "$os" == "Linux" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        if command -v xvfb-run >/dev/null; then
            xvfb-run -a -s "-screen 0 1920x1080x24" ctest --test-dir "$build_dir" --output-on-failure
        else
            echo "No display and no xvfb-run: GPU tests will skip." >&2
            ctest --test-dir "$build_dir" --output-on-failure
        fi
    else
        ctest --test-dir "$build_dir" --output-on-failure
    fi
fi

# --- Package ------------------------------------------------------------------

dist="$ROOT/dist"

if [[ $make_appimage -eq 1 ]]; then
    log "Packaging AppImage"
    appdir="$dist/AppDir"
    tools="$ROOT/.cache/tools"
    output="$dist/Easel-linux-x86_64.AppImage"
    rm -rf "$appdir" "$output"
    mkdir -p "$dist" "$tools"

    cmake --install "$build_dir" --prefix "$appdir/usr"

    for tool in linuxdeploy linuxdeploy-plugin-qt; do
        file="$tools/$tool-x86_64.AppImage"
        if [[ ! -x "$file" ]]; then
            curl -fsSL -o "$file" "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage"
            chmod +x "$file"
        fi
    done

    # Without FUSE (containers, CI), AppImages must extract themselves to run.
    if ! command -v fusermount >/dev/null && ! command -v fusermount3 >/dev/null; then
        export APPIMAGE_EXTRACT_AND_RUN=1
    fi

    (
        cd "$dist"
        export QMAKE="$qt_dir/bin/qmake"
        export LD_LIBRARY_PATH="$qt_dir/lib:${LD_LIBRARY_PATH:-}"
        export PATH="$tools:$PATH"
        export LDAI_OUTPUT="$output"
        export OUTPUT="$output"
        "$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" --plugin qt --output appimage --desktop-file "$appdir/usr/share/applications/easel.desktop" --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/easel.png"
    )
    echo "Wrote $output"
fi

if [[ $make_dmg -eq 1 ]]; then
    log "Packaging dmg"
    stage="$dist/macos"
    output="$dist/Easel-macos.dmg"
    rm -rf "$stage" "$output"
    mkdir -p "$stage"

    cmake --install "$build_dir" --prefix "$stage"
    codesign --force --deep --sign - "$stage/Easel.app"
    ln -s /Applications "$stage/Applications"
    hdiutil create -volname Easel -srcfolder "$stage" -ov -format UDZO "$output"
    echo "Wrote $output"
fi

# --- Run ----------------------------------------------------------------------

if [[ $run_app -eq 1 ]]; then
    log "Launching"
    if [[ "$os" == "Darwin" ]]; then
        open "$build_dir/src/app/Easel.app"
    else
        "$build_dir/src/app/easel"
    fi
fi
