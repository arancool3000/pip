#!/bin/sh
# Builds Pip.app (the on-screen pointer, pen and fn push-to-talk). Needs Xcode's command line tools.
cd "$(dirname "$0")"
mkdir -p Pip.app/Contents/MacOS
cp Info.plist Pip.app/Contents/Info.plist
swiftc -O PetOverlay.swift -o Pip.app/Contents/MacOS/PetOverlay
codesign -s - --force Pip.app
echo "built overlay/Pip.app"
