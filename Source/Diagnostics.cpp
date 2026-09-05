#include "Diagnostics.h"
#include <os/log.h>

namespace diagnostics
{
void trace (const juce::String& message)
{
    static os_log_t handle = os_log_create ("audio.jenya.dereverb2", "engine");
    os_log (handle, "%{public}s", message.toRawUTF8());
}
}
