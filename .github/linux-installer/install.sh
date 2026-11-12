#!/bin/bash
# wfview AppImage Installer
# This script runs from within the makeself self-extracting archive.
# Shared by the x86_64 and arm64 Linux jobs in .github/workflows/build-and-release.yml.
#
# Usage:
#   ./wfview-linux-installer.sh          # non-root install to ~/.local
#   sudo ./wfview-linux-installer.sh     # root install to /usr/local
#
# (The arm64 build is named wfview-linux-arm64-installer.sh.)
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPIMAGE_NAME="wfview.AppImage"

# ─── Detect root vs non-root ──────────────────────────────────
if [ "$(id -u)" -eq 0 ]; then
    IS_ROOT=true
    PREFIX="/usr/local"
    echo "=== wfview installer (root mode) ==="
else
    IS_ROOT=false
    PREFIX="${HOME}/.local"
    echo "=== wfview installer (user mode) ==="
fi
echo "Installing to ${PREFIX}"

BIN_DIR="${PREFIX}/bin"
SHARE_DIR="${PREFIX}/share"
DESKTOP_DIR="${SHARE_DIR}/applications"
ICON_DIR="${SHARE_DIR}/icons/hicolor/256x256/apps"
METAINFO_DIR="${SHARE_DIR}/metainfo"
WFVIEW_SHARE_DIR="${SHARE_DIR}/wfview"
RIGS_DIR="${WFVIEW_SHARE_DIR}/rigs"

# ─── Create directories ───────────────────────────────────────
echo "Creating directories..."
mkdir -p "${BIN_DIR}"
mkdir -p "${DESKTOP_DIR}"
mkdir -p "${ICON_DIR}"
mkdir -p "${METAINFO_DIR}"
mkdir -p "${WFVIEW_SHARE_DIR}"
mkdir -p "${RIGS_DIR}"

# ─── Backup existing rig files (non-root only) ────────────────
if [ "${IS_ROOT}" = false ] && [ -d "${RIGS_DIR}" ] && [ "$(ls -A "${RIGS_DIR}" 2>/dev/null)" ]; then
    BACKUP_NAME="wfview-rigs-backup-$(date +%Y%m%d-%H%M%S).tgz"
    BACKUP_PATH="${WFVIEW_SHARE_DIR}/${BACKUP_NAME}"
    echo "Backing up existing rig files to ${BACKUP_PATH} ..."
    tar czf "${BACKUP_PATH}" -C "${WFVIEW_SHARE_DIR}" rigs/
    echo "Backup created."
fi

# ─── Offer to back up prior wfview binaries ───────────────────
PRIOR_BINS=()
[ -f /usr/local/bin/wfview ] && PRIOR_BINS+=("/usr/local/bin/wfview")
[ -f /usr/bin/wfview ]       && PRIOR_BINS+=("/usr/bin/wfview")

if [ ${#PRIOR_BINS[@]} -gt 0 ]; then
    echo ""
    echo "Existing wfview binary detected at:"
    for b in "${PRIOR_BINS[@]}"; do echo "  $b"; done
    echo ""
    if [ "${IS_ROOT}" = true ]; then
        MV_CMD=(mv -f)
    else
        MV_CMD=(sudo mv -f)
        echo "Note: sudo will be used to move the existing binary (you may be prompted for your password)."
    fi
    if [ -t 0 ]; then
        read -r -p "Back up existing binary to <path>-prior before installing? [Y/n] " REPLY
    else
        REPLY="Y"
        echo "Non-interactive shell; backing up by default."
    fi
    case "${REPLY}" in
        ""|y|Y|yes|YES|Yes)
            for b in "${PRIOR_BINS[@]}"; do
                echo "Moving $b -> ${b}-prior"
                "${MV_CMD[@]}" "$b" "${b}-prior"
            done
            ;;
        *)
            echo "Skipping backup of prior wfview binaries."
            ;;
    esac
fi

# ─── Install AppImage ─────────────────────────────────────────
echo "Installing AppImage to ${BIN_DIR}/wfview ..."
cp "${SCRIPT_DIR}/${APPIMAGE_NAME}" "${BIN_DIR}/wfview"
chmod +x "${BIN_DIR}/wfview"

# ─── Install rig files ────────────────────────────────────────
if [ -d "${SCRIPT_DIR}/rigs" ]; then
    echo "Installing rig files to ${RIGS_DIR}/ ..."
    cp -r "${SCRIPT_DIR}/rigs/"* "${RIGS_DIR}/"
fi

# ─── Install icon ────────────────────────────────────────────
if [ -f "${SCRIPT_DIR}/wfview.png" ]; then
    echo "Installing icon ..."
    cp "${SCRIPT_DIR}/wfview.png" "${ICON_DIR}/wfview.png"
fi

# ─── Install metainfo ────────────────────────────────────────
if [ -f "${SCRIPT_DIR}/org.wfview.wfview.metainfo.xml" ]; then
    echo "Installing metainfo ..."
    cp "${SCRIPT_DIR}/org.wfview.wfview.metainfo.xml" "${METAINFO_DIR}/"
fi

# ─── Install desktop file (rewrite Exec/Icon paths) ──────────
if [ -f "${SCRIPT_DIR}/wfview.desktop" ]; then
    echo "Installing desktop launcher ..."
    sed -e "s|^Exec=.*|Exec=${BIN_DIR}/wfview|" \
        -e "s|^Icon=.*|Icon=${ICON_DIR}/wfview.png|" \
        "${SCRIPT_DIR}/wfview.desktop" > "${DESKTOP_DIR}/wfview.desktop"
    chmod +x "${DESKTOP_DIR}/wfview.desktop"
fi

# ─── Update desktop database (best-effort) ────────────────────
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "${DESKTOP_DIR}" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache "${SHARE_DIR}/icons/hicolor" 2>/dev/null || true
fi

# ─── Summary ─────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════"
echo "  wfview installed successfully!"
echo "════════════════════════════════════════════════"
echo "  Binary:      ${BIN_DIR}/wfview"
echo "  Rig files:   ${RIGS_DIR}/"
echo "  Desktop:     ${DESKTOP_DIR}/wfview.desktop"
echo "  Icon:        ${ICON_DIR}/wfview.png"
echo "════════════════════════════════════════════════"
echo ""
if [ "${IS_ROOT}" = false ]; then
    echo "NOTE: Make sure ${BIN_DIR} is in your PATH."
    echo "  export PATH=\"${BIN_DIR}:\${PATH}\""
fi
echo "Run wfview with:  wfview"
