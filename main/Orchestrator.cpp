#include "Orchestrator.hpp"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <algorithm>
#include <numeric>

void Orchestrator::Run() {
    Timer BOOT;

    esp_vfs_littlefs_conf_t CFG{}; CFG.base_path = "/littlefs"; CFG.partition_label = "littlefs"; CFG.format_if_mount_failed = true;
    if (esp_vfs_littlefs_register(&CFG) != ESP_OK) { ESP_LOGE("BOOT", "LittleFS mount failed"); return; }

    // UNCOMMENT ONLY ON FLASH ERASE: mkdir("/littlefs/Logs", 0777);
    static Hardware BOARD; static Orchestrator ORCH(BOARD); BOARD.Initialize(); static Network NET(BOARD);

    esp_err_t E = nvs_flash_init();
    if (E == ESP_ERR_NVS_NO_FREE_PAGES || E == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); E = nvs_flash_init(); }
    if (E != ESP_OK) { ORCH._Log.Exception("NVS init failed"); return; }

    const esp_partition_t* RUNNING = esp_ota_get_running_partition();
    ORCH._Log.Metrics("Partition: [{}] | Subtype [{}]", RUNNING->label, static_cast<int>(RUNNING->subtype));

    static UI SCREEN(BOARD); static Drive MOTORS(BOARD);
    MOTORS.Initialize(); SCREEN.Initialize(); NET.Initialize(); ORCH.Initialize();

    if (BOARD.Exceptions() != 0) { BOARD.BLK(5, 25, 0, 0); } else { BOARD.RGB(0, 25, 0); }
    esp_ota_mark_app_valid_cancel_rollback();
    ORCH._Log.Metrics("Boot Successful: [{}] | Exceptions: [{}]", BOOT.Elapsed(), BOARD.Summary());
}

void Orchestrator::Initialize() {
    using namespace Configuration::Orchestrator;
    if (xTaskCreatePinnedToCore([](void* P) { static_cast<Orchestrator*>(P)->_Task(); }, "OrchTask", STACK, this, PRIORITY, nullptr, CORE) != pdPASS) _Log.Exception("OrchTask creation failed");
}

void Orchestrator::_Task() {
    using namespace Configuration::Orchestrator; using Device = Hardware::Device; using Expression = Hardware::Expression;
    TickType_t TIC = xTaskGetTickCount(); std::array<uint16_t, SAMPLES> BUF{}; uint8_t TICKS = 0, IDX = 0, CNT = 0; bool ASLEEP = false, WORRY = false;

    while (true) {
        vTaskDelayUntil(&TIC, pdMS_TO_TICKS(TICKMS));

        // Battery: 1Hz sample; publish only when the window is stable; the percentage only ratchets down unless it jumps (recharge)
            if (++TICKS >= SAMPLEMOD) {
            TICKS = 0;
            if (uint32_t MV = _Board.Halted() ? 0 : _Board.Voltage()) {
                BUF[IDX] = MV; IDX = (IDX + 1) % SAMPLES; CNT = std::min<uint8_t>(CNT + 1, SAMPLES);
                auto [LO, HI] = std::minmax_element(BUF.begin(), BUF.end());
                if (CNT == SAMPLES && *HI - *LO < SPREADMV) {
                    uint32_t AVG = std::accumulate(BUF.begin(), BUF.end(), 0u) / SAMPLES; uint8_t NEWPCT = Hardware::Charge(AVG); _Board._MV.store(AVG, std::memory_order_relaxed);
                    if (NEWPCT < _Board.PCT() || NEWPCT > _Board.PCT() + JUMPPCT) { _Board._Pct.store(NEWPCT, std::memory_order_relaxed); _Log.Metrics("Battery: [{}%] [{}mV]", NEWPCT, AVG); }
                }
            }
        }

        // Expression: DEAD > SLEEPY > WORRIED > HAPPY, with hysteresis on the latter two
        uint8_t LEVEL = _Board.PCT(); float DIST = _Board.Distance();
        ASLEEP = !_Board.Exception(Device::BATTERY) && (LEVEL <= SLEEPYIN || (ASLEEP && LEVEL < SLEEPYOUT));
        WORRY = _Board.Mode() != Hardware::Action::FORWARD || DIST < WORRYON || (WORRY && DIST < WORRYOFF);
        _Board.SetExpression(_Board.Exceptions() & (Device::MOTORS | Device::GYRO | Device::USD) ? Expression::DEAD : ASLEEP ? Expression::SLEEPY : WORRY ? Expression::WORRIED : Expression::HAPPY);
    }
}

extern "C" void app_main() {
    Orchestrator::Run();
}
