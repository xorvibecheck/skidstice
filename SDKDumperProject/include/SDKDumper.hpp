#pragma once

#include <Windows.h>
#include <string>

namespace SDKDumper {
    // Start the dump in a background thread; safe to call from DllMain.
    void StartDumpThread();
}
