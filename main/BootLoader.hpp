#pragma once
#include <esp_event.h>

// ------- BootLoader -------
class BootLoader {
public:
    static void Run();
private:
    static void _Reconnect(void* Arg, esp_event_base_t Base, int32_t ID, void* Data);
};
