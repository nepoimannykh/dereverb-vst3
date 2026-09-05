#!/bin/bash
# Prints the plug-in's lifecycle trace from the macOS unified log. Works even when the
# plug-in is running inside a sandboxed host such as the App Store build of DaVinci
# Resolve, which is why this is preferred over the file log for diagnosis.
#
#   processor constructed ... -> the binary loaded and the host instantiated it
#   createEditor called       -> the host asked for the UI
#   editor constructed WxH    -> the UI was built successfully
#   prepareToPlay rate=...    -> audio setup ran; status= says if the model is running
#   (no output at all)        -> the host never loaded the binary
WINDOW="${1:-30m}"
log show --last "$WINDOW" --info --debug --style compact \
    --predicate 'subsystem == "audio.jenya.dereverb2"' 2>/dev/null \
  | sed 's/.*\[audio.jenya.dereverb2:engine\] //'
