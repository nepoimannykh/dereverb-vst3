#pragma once

#include <JuceHeader.h>

// Lifecycle tracing that survives a sandboxed host. File logging depends on a writable
// location the sandbox may deny; os_log always reaches the unified log, so the plug-in can
// report how far it got even when it never manages to show a window.
//
// Read it back with:
//   log show --last 30m --predicate 'subsystem == "audio.jenya.dereverb2"' --info --debug
namespace diagnostics
{
void trace (const juce::String& message);
}
