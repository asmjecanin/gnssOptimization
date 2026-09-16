#!/usr/bin/env bash
# Build a .deb installer for GNSS Positioning on Linux.
#
# Run:  chmod +x make-linux-installer.sh && ./make-linux-installer.sh
#
# What it does: installs prerequisites, builds Release, sets up ~/Setups,
# runs SetupCollector, and tells you what it produced.
#
# IMPORTANT: this script (and the installer/gnssOptimization.xml config it
# uses) both expect the repo to be at $HOME/GNSSOptimization. If you're
# running this on a Linux box where you cloned the repo somewhere else,
# either move/symlink it there, or edit PROJECT_REL below to match.

set -euo pipefail

# ---------------------------------------------------------------- edit these
APP="gnssOptimization"                        # GUI target name (GNSS_APP_NAME)
SOLUTION="GNSSOptimization"                   # SOLUTION_NAME from CMakeLists.txt
PROJECT_REL="GNSSOptimization"                # relative to $HOME - must match installer/gnssOptimization.xml's dev=
CONFIG="$HOME/$PROJECT_REL/installer/$APP.xml"
SDK="$HOME/natID.SDK"
UTILS="$HOME/natID.Utils"
RAMDISK="$HOME/natID.RAMDisk"
# ---------------------------------------------------------------------------

PROJECT="$HOME/$PROJECT_REL"
say() { printf '\n\033[1;36m==> %s\033[0m\n' "$1"; }
die() { printf '\n\033[1;31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }

say "Checking layout"
[ -d "$PROJECT" ]      || die "project not found at $PROJECT"
[ -d "$SDK/DevEnv" ]   || die "SDK not found at $SDK (symlink it if it lives elsewhere)"
[ -d "$UTILS/linux" ]  || die "natID.Utils not found at $UTILS"
[ -f "$CONFIG" ]       || die "collector config not found at $CONFIG"
mkdir -p "$RAMDISK"
echo "project : $PROJECT"
echo "config  : $CONFIG"

say "Installing prerequisites (patchelf is required by the collector)"
sudo apt-get update -qq
sudo apt-get install -y build-essential cmake git libgtk-4-dev libadwaita-1-dev patchelf

say "Building Release"
cmake -S "$PROJECT/Implementation" -B "$PROJECT/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$PROJECT/build" -j"$(nproc)"

BIN="$RAMDISK/Out/$SOLUTION/Release/$APP"
[ -x "$BIN" ] || die "executable not found at $BIN - check SOLUTION/APP names"
echo "built: $BIN"

say "Libraries this binary needs (each must be covered by a <Package> in the config)"
ldd "$BIN" | grep -v "linux-vdso\|/lib/x86_64\|/lib64" || true

say "Preparing ~/Setups (never edit inside the SDK itself)"
rm -rf "$HOME/Setups"
cp -r "$SDK/DevEnv/SetupCollectors" "$HOME/Setups"
cp "$CONFIG" "$HOME/Setups/"

# The SDK maps $RAMDisk to /media/RAMDisk on Linux. Bridge it with a symlink
# instead of editing EnvVariables.xml, so this survives an SDK update.
if [ ! -e /media/RAMDisk ] || [ "$(readlink -f /media/RAMDisk)" != "$RAMDISK" ]; then
	say "Bridging /media/RAMDisk -> $RAMDISK (needs sudo)"
	sudo rm -rf /media/RAMDisk
	sudo ln -s "$RAMDISK" /media/RAMDisk
fi

say "Running SetupCollector"
chmod +x "$UTILS/linux/SetupCollector"
"$UTILS/linux/SetupCollector" "$HOME/Setups/$APP.xml"

say "Output"
find "$RAMDISK/Setup" -maxdepth 2 -name "*.deb" -print
DEB=$(find "$RAMDISK/Setup" -name "*.deb" | head -1)
[ -n "$DEB" ] || die "no .deb produced - read the collector log above"

command cat <<EOF

Done.  Package: $DEB

Install it with apt (dpkg -i will NOT resolve the GTK dependencies):
    sudo apt install $DEB

Then verify it is really standalone - these must resolve inside /usr/lib,
not inside your SDK:
    ldd /usr/bin/$APP | grep -i "natGUI\|mainUtils\|Matrix"

Uninstall with:
    sudo apt remove \$(basename "$DEB" .deb | tr '[:upper:]' '[:lower:]')
EOF
