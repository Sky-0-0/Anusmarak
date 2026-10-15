#include "BootLoader.hpp"
#include "Utilities.hpp"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

void BootLoader::Run() {
    using namespace Configuration::Hardware;
    Timer BOOT;

    esp_vfs_littlefs_conf_t CFG{}; CFG.base_path = "/littlefs"; CFG.partition_label = "littlefs"; CFG.format_if_mount_failed = true;
    if (esp_vfs_littlefs_register(&CFG) != ESP_OK) { ESP_LOGE("BOOT", "LittleFS mount failed"); return; }

    // UNCOMMENT ONLY ON FLASH ERASE: mkdir("/littlefs/Logs", 0777);
    Logger LOG{"BOOT", Configuration::Logger::LF::UTILITIES};
    static Hardware BOARD; BOARD.Initialize();

    esp_err_t E = nvs_flash_init();
    if (E == ESP_ERR_NVS_NO_FREE_PAGES || E == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); E = nvs_flash_init(); }
    if (E != ESP_OK) { LOG.Exception("NVS init failed"); return; }

    static Drive MOTORS(BOARD); static Power BATTERY(BOARD); static Audio AU(BOARD); static UI SCREEN(BOARD, MOTORS, AU); static Network NET(BOARD, BATTERY, SCREEN);
    MOTORS.Initialize(); BATTERY.Initialize(); AU.Initialize(); SCREEN.Initialize(); NET.Initialize();
    if (BOARD.Exceptions() != 0) { BOARD.BLK(5, 25, 0, 0); } else { BOARD.RGB(0, 25, 0); }
    LOG.Metrics("Boot Successful: [{}] | Exceptions: [{}]", BOOT.Elapsed(), BOARD.Summary());

}

extern "C" void app_main() {
    BootLoader::Run();
}
