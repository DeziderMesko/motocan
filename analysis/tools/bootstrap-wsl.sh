#!/usr/bin/env bash
# Turn a fresh WSL2 Ubuntu into the analysis laptop. Run once, at a desk, on a network.
#
#   ./bootstrap-wsl.sh [--no-vision] [--dir DIR] [--skills]
#
# This is not a step in the numbered workflow -- it is what happens before there is a
# checkout to run a step from. It installs the packages the analysis chain needs, Claude
# Code, and gh; authenticates git through gh; clones motocan; and hands over to
# tools/0-setup.sh for the venv. After it, tools/1-selftest.sh should pass with no
# hardware attached.
#
# It deliberately installs nothing for the bluecan half -- no arduino-cli, no ESP32 core,
# and it does not clone the racechrono-canbus fork. This machine reads the bus and works
# out what the bytes mean; the device that streams them is built elsewhere.
#
# The repository is public since 2026-10-12, so the script can be fetched on its own:
#   curl -fsSLO https://raw.githubusercontent.com/DeziderMesko/motocan/main/analysis/tools/bootstrap-wsl.sh
# Copying it through Windows instead brings CRLF line
# endings with it and the shebang then fails with `env: bash\r: No such file or
# directory`; fix that with `sed -i 's/\r$//' bootstrap-wsl.sh` before running it.
set -euo pipefail

WITH_VISION=1
WITH_SKILLS=0
DIR="$HOME/projects/motocan"
REPO="DeziderMesko/motocan"

while (( $# )); do
    case "$1" in
        --no-vision) WITH_VISION=0; shift ;;
        --skills)    WITH_SKILLS=1; shift ;;
        --dir)       DIR="$2"; shift 2 ;;
        -h|--help)   sed -n '2,4p' "$0"; exit 0 ;;
        *)           echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

# ---------------------------------------------------------------- preflight

[[ $EUID -ne 0 ]] || { echo "run as your normal user, not root -- it sudos where it must" >&2; exit 1; }
command -v apt-get >/dev/null || { echo "this expects Debian/Ubuntu" >&2; exit 1; }

grep -qi microsoft /proc/version || echo ">> warning: this does not look like WSL; the usbip parts will be useless"

# The one failure worth catching before anything is installed. CAN support is a kernel
# question, not a package question: older WSL2 kernels shipped without CONFIG_CAN and
# CONFIG_CAN_GS_USB, and the way out is `wsl --update` on Windows, not apt. Everything
# below would install cleanly and then the CANable would never become can0.
echo ">> checking the kernel has CAN ($(uname -r))"
MISSING=()
for m in can vcan can-raw gs_usb; do
    modinfo "$m" >/dev/null 2>&1 || MISSING+=("$m")
done
if (( ${#MISSING[@]} )); then
    cat >&2 <<EOF

!! this kernel is missing: ${MISSING[*]}

   Run 'wsl --update' in an administrator PowerShell on Windows, then 'wsl --shutdown',
   then start the distribution again and re-run this script. Kernel 6.18.x has all four
   built as modules; building a custom kernel is not necessary and not wanted.
EOF
    exit 1
fi

# ---------------------------------------------------------------- packages

echo ">> installing packages"
sudo apt-get update -qq
sudo apt-get install -y -qq \
    git curl ca-certificates gnupg \
    can-utils iproute2 \
    python3-venv python3-pip \
    linux-tools-virtual hwdata \
    gh

# usbipd-win forwards the CANable in from Windows; this is the Linux end of it. The
# usbip client comes from a linux-tools package built for a different kernel version than
# the running WSL one, which is normal -- it talks to vhci-hcd over sysfs, not a
# version-locked ABI.
USBIP="$(ls /usr/lib/linux-tools/*/usbip 2>/dev/null | tail -1 || true)"
if [[ -n "$USBIP" ]]; then
    echo ">> selecting the usbip client ($USBIP)"
    sudo update-alternatives --install /usr/local/bin/usbip usbip "$USBIP" 20
else
    echo ">> warning: no usbip binary under /usr/lib/linux-tools -- the CANable cannot be attached" >&2
fi

# can0 comes up through sudo, but serial devices do not.
if ! id -nG "$USER" | grep -qw dialout; then
    echo ">> adding $USER to dialout (takes effect after 'wsl --shutdown')"
    sudo usermod -aG dialout "$USER"
fi

# ---------------------------------------------------------------- claude code

if command -v claude >/dev/null; then
    echo ">> claude already installed ($(claude --version))"
else
    echo ">> installing claude code"
    curl -fsSL https://claude.ai/install.sh | bash
    export PATH="$HOME/.local/bin:$PATH"
fi

# ---------------------------------------------------------------- git identity and auth

git config --global user.name  >/dev/null 2>&1 || echo "!! git user.name is not set:  git config --global user.name  \"...\""
git config --global user.email >/dev/null 2>&1 || echo "!! git user.email is not set: git config --global user.email \"...\""
git config --global pull.ff only

# gh over HTTPS rather than a copied ssh key: the key stays on the machine that generated
# it, and revoking this laptop later is one click on github.com instead of a hunt for
# which key was which.
if gh auth status >/dev/null 2>&1; then
    echo ">> gh already authenticated"
else
    echo ">> authenticating gh -- follow the prompts (a code to paste into a browser)"
    gh auth login --hostname github.com --git-protocol https --web
fi
gh auth setup-git

# ---------------------------------------------------------------- the repository

if [[ -d "$DIR/.git" ]]; then
    echo ">> $DIR exists, pulling"
    git -C "$DIR" pull --ff-only
else
    echo ">> cloning $REPO into $DIR"
    mkdir -p "$(dirname "$DIR")"
    gh repo clone "$REPO" "$DIR"
fi

# The vendored CSS skills are off by default in the repository because switching them on
# changes what every session in it can do. On a machine whose only job is the analysis,
# that is the point -- but as an untracked symlink, so it stays a property of this laptop.
if (( WITH_SKILLS )); then
    echo ">> enabling the cansub-reverse-engineering skill for this checkout"
    mkdir -p "$DIR/.claude/skills"
    ln -sfn ../../analysis/css-skill/.claude/skills/cansub-reverse-engineering \
        "$DIR/.claude/skills/cansub-reverse-engineering"
fi

# ---------------------------------------------------------------- the venv

echo ">> building the analysis venv"
if (( WITH_VISION )); then
    "$DIR/analysis/tools/0-setup.sh" --with-vision
else
    "$DIR/analysis/tools/0-setup.sh"
fi

cat <<NEXT

>> done. Three things are left, and only the first needs anything of you now:

   1. Log in to Claude Code, once:            claude
   2. Prove the chain, no hardware needed:    cd $DIR/analysis && ./tools/1-selftest.sh
   3. When the CANable is plugged into THIS machine, from an administrator PowerShell
      on Windows -- usbipd is a Windows program, not the 'usbip' client installed here:
        winget install --exact dorssel.usbipd-win   # once per machine, then reopen it
        usbipd list
        usbipd bind --busid <BUSID>                       # once per device, persists
        usbipd attach --wsl --busid <BUSID> --auto-attach  # per session
      bind is per machine -- the desktop's bind does not carry over.

   If you were added to dialout just now, run 'wsl --shutdown' on Windows first.
NEXT
