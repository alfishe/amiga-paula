#!/bin/bash
#
# macOS Code Signing and Notarization Helper
#
# Usage:
#   ./macos-codesign.sh bypass              - Remove quarantine (run unsigned)
#   ./macos-codesign.sh sign                - Sign with ad-hoc signature
#   ./macos-codesign.sh sign <identity>     - Sign with Developer ID
#   ./macos-codesign.sh notarize <identity> - Sign and notarize
#
# For notarization, set environment variables:
#   APPLE_ID        - Your Apple ID email
#   APPLE_TEAM_ID   - Your Team ID
#   APPLE_PASSWORD  - App-specific password (or @keychain:notarytool)
#

set -e

BINARY="modplayer"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY_PATH="${SCRIPT_DIR}/../build/${BINARY}"

if [ ! -f "$BINARY_PATH" ]; then
    BINARY_PATH="${SCRIPT_DIR}/../${BINARY}"
fi

if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Cannot find ${BINARY}"
    exit 1
fi

case "$1" in
    bypass)
        echo "Removing quarantine attribute..."
        xattr -d com.apple.quarantine "$BINARY_PATH" 2>/dev/null || true
        echo "Done. You can now run: $BINARY_PATH"
        ;;

    sign)
        IDENTITY="${2:--}"
        echo "Signing with identity: $IDENTITY"
        codesign --force --sign "$IDENTITY" --timestamp --options runtime "$BINARY_PATH"
        codesign -vv "$BINARY_PATH"
        echo "Done."
        ;;

    notarize)
        IDENTITY="$2"
        if [ -z "$IDENTITY" ]; then
            echo "Error: Developer ID identity required"
            echo "Usage: $0 notarize \"Developer ID Application: Your Name (TEAMID)\""
            exit 1
        fi

        if [ -z "$APPLE_ID" ] || [ -z "$APPLE_TEAM_ID" ] || [ -z "$APPLE_PASSWORD" ]; then
            echo "Error: Set APPLE_ID, APPLE_TEAM_ID, and APPLE_PASSWORD environment variables"
            exit 1
        fi

        echo "Signing..."
        codesign --force --sign "$IDENTITY" --timestamp --options runtime "$BINARY_PATH"

        echo "Creating ZIP for notarization..."
        ZIP_PATH="/tmp/${BINARY}-notarize.zip"
        ditto -c -k --keepParent "$BINARY_PATH" "$ZIP_PATH"

        echo "Submitting for notarization..."
        xcrun notarytool submit "$ZIP_PATH" \
            --apple-id "$APPLE_ID" \
            --team-id "$APPLE_TEAM_ID" \
            --password "$APPLE_PASSWORD" \
            --wait

        rm "$ZIP_PATH"
        echo "Done. Binary is signed and notarized."
        ;;

    *)
        echo "macOS Code Signing Helper"
        echo ""
        echo "Usage:"
        echo "  $0 bypass              - Remove quarantine (quick fix for unsigned binary)"
        echo "  $0 sign                - Sign with ad-hoc signature (local use only)"
        echo "  $0 sign <identity>     - Sign with Developer ID"
        echo "  $0 notarize <identity> - Sign and notarize for distribution"
        echo ""
        echo "Examples:"
        echo "  $0 bypass"
        echo "  $0 sign"
        echo "  $0 sign \"Developer ID Application: John Doe (ABCD1234)\""
        echo ""
        echo "For notarization, set environment variables:"
        echo "  export APPLE_ID=\"your@email.com\""
        echo "  export APPLE_TEAM_ID=\"ABCD1234\""
        echo "  export APPLE_PASSWORD=\"xxxx-xxxx-xxxx-xxxx\""
        ;;
esac
