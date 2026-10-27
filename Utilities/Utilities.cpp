#include "Utilities.hpp"
#include <ctime>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <freertos/task.h>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <esp_rom_sys.h>
#include <esp_partition.h>
#include "mdns.h"

// ------- Logger -------
Logger::Logger(const char* M, LF F) : _M(M), _F(F) {
    using namespace Configuration::Logger;
    if (!_Queue) {
        _Queue = xQueueCreate(DEPTH, sizeof(Entry));
        assert(_Queue);
        [[maybe_unused]] auto CRT = xTaskCreatePinnedToCore(_ProcessQueue, "LogTask", STACK, nullptr, PRIORITY, nullptr, CORE);
        assert(CRT == pdPASS);
    }
    size_t IDX = static_cast<size_t>(_F);
    if (_Files[IDX] == -1) {
        _Files[IDX] = open(PATHS[IDX].data(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (_Files[IDX] < 0) std::fprintf(stderr, "Logger: Open failed [%d]: %s\n", errno, strerror(errno));
    }
}

void Logger::_ProcessQueue(void*) {
    using namespace Configuration::Logger;
    Entry I;
    while (xQueueReceive(_Queue, &I, portMAX_DELAY) == pdTRUE) {
        uint32_t S = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
        std::array<char, TIMELEN> TBUF{};
        std::snprintf(TBUF.data(), TBUF.size(), "[%02lu:%02lu:%02lu]", S / 3600, (S / 60) % 60, S % 60);
        std::array<char, BUFLEN> BUF{};
        int LEN = std::snprintf(BUF.data(), BUF.size(), "%s - [%s] [%s] %s\n", TBUF.data(), LEVELS[I.L].data(), I.M, I.MSG.data());
        if (LEN <= 0) continue;
        std::fwrite(BUF.data(), 1, LEN, stdout);
        size_t Idx = static_cast<size_t>(I.F);
        if (int FD = _Files[Idx]; FD >= 0) { write(FD, BUF.data(), LEN); fsync(FD); }
    }
}

// ------- Timer -------
std::string_view Timer::_F(int64_t NS) {
    using namespace Configuration::Timer;
    constexpr std::array<int64_t, 4> T{TH, TM, TS, TMS}; size_t I = 0; while (I < T.size() && NS < T[I]) ++I;
    switch (I) {
        case 0: *std::format_to_n(_BUF.data(), _BUF.size() - 1, "{}H {}M", static_cast<int>(NS / TH), static_cast<int>((NS / TM) % 60)).out = '\0'; break;
        case 1: *std::format_to_n(_BUF.data(), _BUF.size() - 1, "{}M {}s", static_cast<int>(NS / TM), static_cast<int>((NS / TS) % 60)).out = '\0'; break;
        case 2: *std::format_to_n(_BUF.data(), _BUF.size() - 1, "{:.2f}s", float(NS) * 1e-9f).out = '\0'; break;
        case 3: *std::format_to_n(_BUF.data(), _BUF.size() - 1, "{:.2f}ms", float(NS) * 1e-6f).out = '\0'; break;
        default: *std::format_to_n(_BUF.data(), _BUF.size() - 1, "{:.2f}μs", float(NS) * 1e-3f).out = '\0'; break;
    }
    return std::string_view(_BUF.data());
}

// ------- Hardware -------
void Hardware::Initialize() {
    Timer T; using namespace Configuration::Hardware;
    temperature_sensor_config_t TCF = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100); esp_err_t E = temperature_sensor_install(&TCF, &_TSH); if (E == ESP_OK) E = temperature_sensor_enable(_TSH); _Report(Device::TEMPERATURE, E != ESP_OK);
    led_strip_config_t SCF{}; SCF.strip_gpio_num = GPIO_NUM_48; SCF.max_leds = 1; SCF.led_model = LED_MODEL_WS2812; SCF.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t RCF{}; RCF.clk_src = RMT_CLK_SRC_DEFAULT; RCF.resolution_hz = 10000000; _Report(Device::RGB, led_strip_new_rmt_device(&SCF, &RCF, &_LED) != ESP_OK);
    adc_oneshot_unit_init_cfg_t UCF{}; UCF.unit_id = ADC_UNIT_1; adc_oneshot_chan_cfg_t ACF{}; ACF.atten = ADC_ATTEN_DB_12; ACF.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_cali_curve_fitting_config_t KCF{}; KCF.unit_id = ADC_UNIT_1; KCF.chan = ADC_CHANNEL_0; KCF.atten = ADC_ATTEN_DB_12; KCF.bitwidth = ADC_BITWIDTH_DEFAULT;
    esp_err_t AE = adc_oneshot_new_unit(&UCF, &_ADC); AE |= adc_oneshot_config_channel(_ADC, ADC_CHANNEL_0, &ACF); AE |= adc_cali_create_scheme_curve_fitting(&KCF, &_CLB);
    uint32_t V = Voltage(); _Report(Device::BATTERY, AE != ESP_OK || V == 0); _MV.store(V, std::memory_order_relaxed); _Pct.store(Charge(V), std::memory_order_relaxed);
    i2c_master_bus_config_t BCF{}; BCF.i2c_port = I2C_NUM_0; BCF.sda_io_num = SDAPIN; BCF.scl_io_num = SCLPIN; BCF.clk_source = I2C_CLK_SRC_DEFAULT; BCF.glitch_ignore_cnt = 7; BCF.flags.enable_internal_pullup = true; _Report(Device::GYRO, i2c_new_master_bus(&BCF, &_I2C) != ESP_OK);
    spi_bus_config_t SPIBCF{}; SPIBCF.sclk_io_num = TFTPINS[0]; SPIBCF.mosi_io_num = TFTPINS[1]; SPIBCF.miso_io_num = -1; SPIBCF.quadwp_io_num = -1; SPIBCF.quadhd_io_num = -1; SPIBCF.max_transfer_sz = Configuration::UI::WIDTH * Configuration::UI::STRIPROWS * sizeof(uint16_t); _Report(Device::TFT, spi_bus_initialize(SPI2_HOST, &SPIBCF, SPI_DMA_CH_AUTO) != ESP_OK);  _Log.Metrics("Hardware Initialized in [{}]", T.Elapsed());
}

std::string_view Hardware::Summary() { char* P = _BUF.data(); for (size_t I = 0; I < Configuration::Hardware::DEVICES.size(); ++I) if (_Exceptions >> I & 1) { P = std::format_to_n(P, _BUF.data() + _BUF.size() - 1 - P, "{}{}", P == _BUF.data() ? "" : ", ", Configuration::Hardware::DEVICES[I]).out; } *P = '\0'; return P == _BUF.data() ? "None" : std::string_view(_BUF.data(), P - _BUF.data()); }
uint32_t Hardware::Voltage() const { int RAW = 0, MV = 0, SUM = 0; using namespace Configuration::Hardware; for (uint16_t I = 0; I < BATSAMPLES; ++I) { if (adc_oneshot_read(_ADC, ADC_CHANNEL_0, &RAW) != ESP_OK || adc_cali_raw_to_voltage(_CLB, RAW, &MV) != ESP_OK) return 0; SUM += MV; } return SUM * BATNUM / (BATSAMPLES * BATDEN); }

// ------- Drive -------
void Drive::Initialize() {
    Timer T; using namespace Configuration::Drive; using namespace Configuration::Hardware;

    // ------- Motors -------
    uint64_t MSM = 0; for (auto P : DIRS) MSM |= 1ULL << P; gpio_config_t DCF{}; DCF.pin_bit_mask = MSM; DCF.mode = GPIO_MODE_OUTPUT; esp_err_t E = gpio_config(&DCF); for (auto P : DIRS) E |= gpio_set_level(P, 0);
    ledc_timer_config_t TCF{}; TCF.speed_mode = LEDC_LOW_SPEED_MODE; TCF.duty_resolution = static_cast<ledc_timer_bit_t>(PWMBITS); TCF.timer_num = LEDC_TIMER_0; TCF.freq_hz = PWMHZ; TCF.clk_cfg = LEDC_AUTO_CLK; E |= ledc_timer_config(&TCF);
    for (int I = 0; I < 4; ++I) { ledc_channel_config_t CCF{}; CCF.gpio_num = ENS[I]; CCF.speed_mode = LEDC_LOW_SPEED_MODE; CCF.channel = static_cast<ledc_channel_t>(I); CCF.timer_sel = LEDC_TIMER_0; E |= ledc_channel_config(&CCF); } _Board._Report(Device::MOTORS, E != ESP_OK);

    // ------- IMU -------
    i2c_device_config_t ICF{}; ICF.dev_addr_length = I2C_ADDR_BIT_LEN_7; ICF.device_address = GYROADDR; ICF.scl_speed_hz = I2CHZ;
    bool GYR = i2c_master_bus_add_device(_Board.I2C(), &ICF, &_IMU) == ESP_OK;
    if (GYR) { for (size_t I = 0; I < GYROINIT.size(); I += 2) { uint8_t CP[2] = {GYROINIT[I], GYROINIT[I+1]}; GYR &= i2c_master_transmit(_IMU, CP, 2, 100) == ESP_OK; } uint8_t ID = 0; GYR = GYR && i2c_master_transmit_receive(_IMU, &GYROID, 1, &ID, 1, 100) == ESP_OK && ID == GYROADDR;
        int32_t SUM = 0; int N = 0; std::array<uint8_t, 2> GB{}; for (int I = 0; GYR && I < BIASN; ++I) { if (i2c_master_transmit_receive(_IMU, &GYROZREG, 1, GB.data(), 2, 100) == ESP_OK) { SUM += int16_t(GB[0] << 8 | GB[1]); ++N; } vTaskDelay(pdMS_TO_TICKS(10)); } GYR = GYR && N > BIASN / 2; _Bias = N ? float(SUM) / float(N) : 0.0f; } _Board._Report(Device::GYRO, !GYR);

    // ------- USD -------
    gpio_set_direction(TRIGPIN, GPIO_MODE_OUTPUT); gpio_set_level(TRIGPIN, 0);
    gpio_config_t GCF{}; GCF.pin_bit_mask = 1ULL << ECHOPIN; GCF.mode = GPIO_MODE_INPUT; GCF.intr_type = GPIO_INTR_ANYEDGE; GCF.pull_down_en = GPIO_PULLDOWN_ENABLE; E = gpio_config(&GCF);
    esp_err_t ISV = gpio_install_isr_service(ESP_INTR_FLAG_IRAM); if (ISV != ESP_OK && ISV != ESP_ERR_INVALID_STATE) E |= ISV; E |= gpio_isr_handler_add(ECHOPIN, _EchoISR, this);
    _Ping(); vTaskDelay(pdMS_TO_TICKS(POSTMS)); _Board._Report(Device::USD, E != ESP_OK || !_EchoUs.exchange(0, std::memory_order_relaxed)); // a live HC-SR04 always completes a pulse (≈38ms with nothing in range)

    // ------- Task Launch -------
    if (!(_Board.Exceptions() & (Device::MOTORS | Device::GYRO | Device::USD))) { if (xTaskCreatePinnedToCore([](void* P) { static_cast<Drive*>(P)->_Task(); }, "DriveTask", STACK, this, PRIORITY, nullptr, CORE) != pdPASS) _Log.Exception("DriveTask creation failed"); } _Log.Metrics("Drive Initialized in [{}]", T.Elapsed());
}

// ------- Drive Runtime Methods -------
IRAM_ATTR void Drive::_EchoISR(void* P) { auto* Z = static_cast<Drive*>(P); int64_t NOW = esp_timer_get_time(); if (GPIO.in & (1ULL << Configuration::Hardware::ECHOPIN)) Z->_EchoStart = NOW; else Z->_EchoUs.store(uint32_t(NOW - Z->_EchoStart), std::memory_order_relaxed); }

void Drive::_Ping() { gpio_set_level(Configuration::Hardware::TRIGPIN, 1); esp_rom_delay_us(10); gpio_set_level(Configuration::Hardware::TRIGPIN, 0); }

void Drive::_Motors(bool LF, bool RF, int LS, int RS) {
    using namespace Configuration::Hardware;
    for (int I = 0; I < 8; I += 2) { bool FWD = I < 4 ? LF : RF; gpio_set_level(DIRS[I], FWD); gpio_set_level(DIRS[I + 1], !FWD); }
    for (int I = 0; I < 4; ++I) { auto CH = static_cast<ledc_channel_t>(I); ledc_set_duty(LEDC_LOW_SPEED_MODE, CH, I < 2 ? LS : RS); ledc_update_duty(LEDC_LOW_SPEED_MODE, CH); }
}

void Drive::_Task() {
    using namespace Configuration::Drive; using namespace Configuration::Hardware;
    TickType_t TIC = xTaskGetTickCount(); Timer TIM; int64_t LST = TIM.Raw(); uint8_t PAS = 0, STAGE = 0; uint16_t WAIT = 0, PASSES = 0; // STAGE: 0 cruise, 1/3/5 pivot (left, right, chosen), 2/4 settle + ping
    float HDG = 0, INT = 0, PRV = 0, PEP = 0, SCA = 100, SET = 0, BASE = 0, LDIST = 0, RDIST = 0;
    bool RUN = true; std::array<uint8_t, 2> GB{};

    while (true) {
        vTaskDelayUntil(&TIC, pdMS_TO_TICKS(TASKMS));
        int64_t NOW = TIM.Raw(); float DT = std::clamp(float(NOW - LST) * 1e-9f, 1e-4f, 0.05f); LST = NOW;

        // Halt (OTA / restart / shutdown) and Gyro Failure: Outputs Off, State Re-latched on Resume
        bool GYROOK = _IMU && i2c_master_transmit_receive(_IMU, &GYROZREG, 1, GB.data(), 2, 100) == ESP_OK;
        if (_Board.Halted() || !GYROOK) { if (!GYROOK && RUN) { _Log.Warn("Gyro read failed"); } RUN = false; _Board._Mode.store(Action::STOP, std::memory_order_relaxed); _Motors(true, true, 0, 0); continue; }
        if (!RUN) { RUN = true; STAGE = 0; SET = HDG; INT = PRV = 0; SCA = 100; PAS = 0; }
        HDG += (int16_t(GB[0] << 8 | GB[1]) - _Bias) / GYROSCALE * DT;

        // Escape: Pivot Left -> Ping -> Pivot Right -> Ping -> Pick Clearer Side, or Turn Back
        float ERR = SET - HDG;
        if (!STAGE) {
            if (PAS % 10 == 0) {
                uint32_t US = _EchoUs.exchange(0, std::memory_order_relaxed); float DIS = std::min(US ? US / USDIV : USMAX, USMAX), EP = DIS - STANDOFF; _Board._Distance.store(DIS, std::memory_order_relaxed); SCA = std::clamp(PDKP * EP + PDKD * (EP - PEP) / 0.1f, 0.0f, 100.0f); PEP = EP;
                if (DIS <= STANDOFF) { STAGE = 1; BASE = HDG; SET = BASE + TURNDEG; PASSES = 0; _Log.Metrics("Obstacle: [{:.1f}cm] - Escape", DIS); } else _Ping();
            }
        } else if (STAGE & 1) {
            bool LATE = ++PASSES > TURNTMO;
            if (fabsf(ERR) < TURNTOL || LATE) {
                if (LATE) _Log.Warn("Pivot timeout: stage {}", STAGE);
                PASSES = 0; WAIT = 2 * SETTLEPASS;
                if (STAGE == 5) { STAGE = 0; SET = HDG; INT = PRV = 0; SCA = 100; PAS = 1; _Ping(); _Log.Metrics("Escape: L [{:.0f}] R [{:.0f}]", LDIST, RDIST); }
                else if (LATE) { STAGE = 5; SET = BASE - 2 * TURNDEG; }
                else ++STAGE;
            }
        } else if (--WAIT == SETTLEPASS) { _EchoUs.exchange(0, std::memory_order_relaxed); _Ping(); }
        else if (!WAIT) {
            uint32_t US = _EchoUs.exchange(0, std::memory_order_relaxed); float DIS = std::min(US ? US / USDIV : USMAX, USMAX); _Board._Distance.store(DIS, std::memory_order_relaxed); PASSES = 0;
            if (STAGE == 2) { LDIST = DIS; STAGE = 3; SET = BASE - TURNDEG; } else { RDIST = DIS; STAGE = 5; SET = std::max(LDIST, RDIST) <= STANDOFF ? BASE - 2 * TURNDEG : LDIST > RDIST ? BASE + TURNDEG : BASE - TURNDEG; }
        }
        PAS = (PAS + 1) % PASSWRAP;

        // Heading PID Control (SET may have changed above)
        ERR = SET - HDG; INT = std::clamp(INT + ERR * DT, -INTEGMAX, INTEGMAX); float OUT = HDGKP * ERR + HDGKI * INT + HDGKD * (ERR - PRV) / DT; PRV = ERR; int D = int(std::clamp(OUT, -127.0f, 127.0f));

        // Differential Drive Mixing & Motor Output
        bool LF = true, RF = true; int LS = 0, RS = 0;
        if (!STAGE) { float B = std::max(DEFAULTSPEED * SCA * 0.01f, float(MINPWM)); LS = int(std::clamp(B - D, 0.0f, 255.0f)); RS = int(std::clamp(B + D, 0.0f, 255.0f)); }
        else if (STAGE & 1) { LF = ERR < 0; RF = ERR > 0; LS = RS = ESCAPEPWM; }
        _Board._Mode.store(!STAGE ? Action::FORWARD : STAGE & 1 ? (ERR > 0 ? Action::LEFT : Action::RIGHT) : Action::STOP, std::memory_order_relaxed);
        _Motors(LF, RF, LS, RS);
    }
}

// ------- UI -------
void UI::Initialize() {
    Timer T; using namespace Configuration::UI; using namespace Configuration::Hardware;
    spi_device_interface_config_t DCF{}; DCF.clock_speed_hz = SPIHZ; DCF.spics_io_num = TFTPINS[4]; DCF.queue_size = 1;
    gpio_config_t GCF{}; GCF.pin_bit_mask = (1ULL << TFTPINS[3]) | (1ULL << TFTPINS[2]) | (1ULL << TFTPINS[5]); GCF.mode = GPIO_MODE_OUTPUT;
    esp_err_t E = spi_bus_add_device(_Board.SPI(), &DCF, &_TFT); E |= gpio_config(&GCF);

    if (E == ESP_OK) {
        gpio_set_level(TFTPINS[2], 0); vTaskDelay(pdMS_TO_TICKS(TFTDELA)); gpio_set_level(TFTPINS[2], 1); vTaskDelay(pdMS_TO_TICKS(TFTDELB));
        for (size_t I = 0, N = 0; TFTINIT[I]; I += 2 + N) { N = TFTINIT[I + 1] & 0x7F; E |= _Display(TFTINIT[I], &TFTINIT[I + 2], N); if (TFTINIT[I + 1] & 0x80) vTaskDelay(pdMS_TO_TICKS(TFTIDEL)); }
        if (xTaskCreatePinnedToCore([](void* P) { static_cast<UI*>(P)->_Task(); }, "UITask", STACK, this, PRIORITY, nullptr, CORE) != pdPASS) { _Log.Exception("UITask creation failed"); }
    }
    _Board._Report(Hardware::Device::TFT, E != ESP_OK); _Log.Metrics("UI Initialized in [{}]", T.Elapsed());
}

void UI::_Task() {
    using namespace Configuration::UI; TickType_t TIC = xTaskGetTickCount(); uint32_t FRAME = 0; uint16_t LASTEH = 0xFFFF, COLOR = 0;
    constexpr int LEFTX = CENTERX - EYEDX, RIGHTX = CENTERX + EYEDX, HALFW = EYEW / 2, X0 = EYECOLS[1], X1 = EYECOLS[3];

    while (true) {
        vTaskDelayUntil(&TIC, pdMS_TO_TICKS(1000 / REFRESHRATE)); if (_Board.Halted()) continue;
        uint16_t EH = (++FRAME % 100) < BLINKFRAMES ? 4 : EYEH;

        uint8_t LEVEL = _Board.PCT();
        if (uint16_t C = RINGCOLORS[LEVEL <= RINGLIMITS[2] ? 3 : LEVEL <= RINGLIMITS[1] ? 2 : LEVEL <= RINGLIMITS[0] ? 1 : 0]; C != COLOR) { // redraw only on a band change; the pass blacks out the centre, so the eyes follow
            COLOR = C; LASTEH = 0xFFFF; _Display(CASET, FULL.data(), FULL.size()); _Display(RASET, FULL.data(), FULL.size());
            for (uint8_t S = 0; S < HEIGHT / STRIPROWS; ++S) {
                uint16_t* P = _Strip.data(); uint16_t RY = S * STRIPROWS;
                for (uint16_t Y = RY; Y < RY + STRIPROWS; ++Y) { int DYS = (int(Y) - CENTERY) * (int(Y) - CENTERY); for (uint16_t X = 0; X < WIDTH; ++X, ++P) { int DS = (int(X) - CENTERX) * (int(X) - CENTERX) + DYS; *P = (DS >= RINGIN * RINGIN && DS <= RINGOUT * RINGOUT) ? C : 0x0000; }}
                _Display(S ? RAMWRC : RAMWR, reinterpret_cast<const uint8_t*>(_Strip.data()), sizeof(_Strip));
            }
            gpio_set_level(Configuration::Hardware::TFTPINS[5], 1); // backlight on only after the first clean frame
        }
        if (EH == LASTEH) continue;
        LASTEH = EH; _Display(CASET, EYECOLS.data(), EYECOLS.size()); _Display(RASET, EYEROWS.data(), EYEROWS.size());
        for (uint8_t S = EYESTARTSTRIP; S <= EYEENDSTRIP; ++S) {
            uint16_t* P = _Strip.data(); uint16_t RY = S * STRIPROWS;
            for (uint16_t Y = RY; Y < RY + STRIPROWS; ++Y) { bool ROW = std::abs(int(Y) - CENTERY) <= EH / 2; for (int X = X0; X <= X1; ++X, ++P) *P = (ROW && (std::abs(X - LEFTX) <= HALFW || std::abs(X - RIGHTX) <= HALFW)) ? EYECOLOR : 0x0000; }
            _Display(S == EYESTARTSTRIP ? RAMWR : RAMWRC, reinterpret_cast<const uint8_t*>(_Strip.data()), (X1 - X0 + 1) * STRIPROWS * sizeof(uint16_t));
        }
    }
}

esp_err_t UI::_Display(uint8_t CMD, const uint8_t* DAT, size_t LEN) { spi_transaction_t TX{}; TX.length = 8; TX.tx_buffer = &CMD; gpio_set_level(Configuration::Hardware::TFTPINS[3], 0); esp_err_t E = spi_device_transmit(_TFT, &TX); if (LEN) { TX.length = LEN * 8; TX.tx_buffer = DAT; gpio_set_level(Configuration::Hardware::TFTPINS[3], 1);  E |= spi_device_transmit(_TFT, &TX);} return E; }

// ------- Network -------
extern "C" const char _binary_Status_html_start; extern "C" const char _binary_Status_html_end;
void Network::_Reconnect(void*, esp_event_base_t, int32_t, void*) { esp_wifi_connect(); }

void Network::_Stop() { _Board._Halt.store(true, std::memory_order_relaxed); vTaskDelay(pdMS_TO_TICKS(Configuration::Network::HALTMS)); }

void Network::Initialize() {
    Timer T; using namespace Configuration::Network; using namespace Configuration::Hardware; wifi_init_config_t INIT = WIFI_INIT_CONFIG_DEFAULT(); wifi_config_t WCFG{};
    strncpy(reinterpret_cast<char*>(WCFG.sta.ssid), SSID.data(), sizeof(WCFG.sta.ssid) - 1); strncpy(reinterpret_cast<char*>(WCFG.sta.password), PASS.data(), sizeof(WCFG.sta.password) - 1); WCFG.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t W = esp_netif_init() | esp_event_loop_create_default(); esp_netif_t* NET = esp_netif_create_default_wifi_sta(); W |= esp_wifi_init(&INIT) | esp_wifi_set_mode(WIFI_MODE_STA) | esp_wifi_set_config(WIFI_IF_STA, &WCFG) | esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM); esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, _Reconnect, nullptr); esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, _Reconnect, nullptr); esp_wifi_connect();

    esp_netif_ip_info_t IP{}; for (uint16_t I = 0; (IP.ip.addr == 0 || IP.ip.addr == 0xFFFFFFFF) && I < 40; ++I) { vTaskDelay(pdMS_TO_TICKS(100)); esp_netif_get_ip_info(NET, &IP); }
    std::array<uint8_t, 6> MACADR; esp_netif_get_mac(NET, MACADR.data()); _Board._Report(Hardware::Device::WIFI, W != ESP_OK); _Log.Metrics("IP → [{}.{}.{}.{}] | MAC → [{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}]", IP.ip.addr & 255, (IP.ip.addr >> 8) & 255, (IP.ip.addr >> 16) & 255, IP.ip.addr >> 24, MACADR[0], MACADR[1], MACADR[2], MACADR[3], MACADR[4], MACADR[5]);

    if (!_Board.Exception(Hardware::Device::WIFI)) {
        mdns_init(); mdns_hostname_set("Anusmarak"); mdns_instance_name_set("Anusmarak ESP32-S3");
        httpd_config_t HCF = HTTPD_DEFAULT_CONFIG(); HCF.core_id = CORE; HCF.task_priority = PRIORITY; HCF.uri_match_fn = httpd_uri_match_wildcard; httpd_handle_t SRV = nullptr;
        httpd_uri_t URI{.uri = URIROOT.data(), .method = HTTP_GET, .handler = _HTML, .user_ctx = this}, LURI{.uri = URILOGS.data(), .method = HTTP_GET, .handler = _Logs, .user_ctx = this}, TURI{.uri = URITELEMETRY.data(), .method = HTTP_GET, .handler = _Telemetry, .user_ctx = this},
        OURI{.uri = "/OTA", .method = HTTP_POST, .handler = _OTA, .user_ctx = this}, RURI{.uri = "/Restart", .method = HTTP_GET, .handler = _Restart, .user_ctx = this};
        if (httpd_start(&SRV, &HCF) != ESP_OK || httpd_register_uri_handler(SRV, &URI) != ESP_OK || httpd_register_uri_handler(SRV, &LURI) != ESP_OK || httpd_register_uri_handler(SRV, &TURI) != ESP_OK || httpd_register_uri_handler(SRV, &OURI) != ESP_OK || httpd_register_uri_handler(SRV, &RURI) != ESP_OK) { _Log.Exception("HTTP server failed"); }}
    _Log.Metrics("Network Initialized in [{}]", T.Elapsed());
}

esp_err_t Network::_HTML(httpd_req_t* REQ) { std::string_view PAGE(&_binary_Status_html_start, &_binary_Status_html_end - &_binary_Status_html_start); httpd_resp_set_type(REQ, "text/html"); return httpd_resp_send(REQ, PAGE.data(), PAGE.size()); }

esp_err_t Network::_Logs(httpd_req_t* R) {
    std::array<char, 128> P{}; auto RES = std::format_to_n(P.data(), P.size() - 1, "/littlefs{}", R->uri); *RES.out = '\0'; FILE* F = std::fopen(P.data(), "rb"); if (!F) return httpd_resp_send_err(R, HTTPD_404_NOT_FOUND, "Not found"), ESP_FAIL;
    httpd_resp_set_type(R, "text/plain"); std::array<char, 256> B{}; size_t L; while ((L = std::fread(B.data(), 1, B.size(), F)) > 0) { httpd_resp_send_chunk(R, B.data(), L); } std::fclose(F); httpd_resp_send_chunk(R, nullptr, 0); return ESP_OK;
}

esp_err_t Network::_Telemetry(httpd_req_t* R) {
    wifi_ap_record_t AP{}; int RSSI = (esp_wifi_sta_get_ap_info(&AP) == ESP_OK) ? AP.rssi : 0; auto* N = static_cast<Network*>(R->user_ctx); std::array<char, 256> B{}; uint32_t UptimeSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    auto RES = std::format_to_n(B.data(), B.size(), R"({{"BPCT":{},"BVLT":{:.2f},"Temperature":{:.1f},"Expression":"{}","RSSI":{},"Exceptions":"{}","Uptime":{},"USD":{:.1f}}})", N->_Board.PCT(), static_cast<float>(N->_Board.MV()) / 1000.0f, N->_Board.Temperature(), N->_Board.EXPR(), RSSI, N->_Board.Summary(), UptimeSec, N->_Board.Distance());
    size_t LEN = static_cast<size_t>(RES.out - B.data()); httpd_resp_set_type(R, "application/json"); return httpd_resp_send(R, B.data(), static_cast<ssize_t>(LEN));
}

esp_err_t Network::_Restart(httpd_req_t* R) { auto* N = static_cast<Network*>(R->user_ctx); N->_Log.Metrics("System Restart"); httpd_resp_send(R, "Restarting...", HTTPD_RESP_USE_STRLEN); N->_Stop(); esp_restart(); return ESP_OK; }


esp_err_t Network::_OTA(httpd_req_t* R) {
    auto* N = static_cast<Network*>(R->user_ctx); std::array<char, 1024> B{}; int L = 0, REM = R->content_len; const char* ERR = nullptr;
    const esp_partition_t* P = esp_ota_get_next_update_partition(nullptr); esp_ota_handle_t H = 0;
    N->_Stop(); // flash erase/write stalls the control loop, so the motors go quiet first
    if (!P || esp_ota_begin(P, OTA_SIZE_UNKNOWN, &H) != ESP_OK) ERR = "Init Failed";
    while (!ERR && REM > 0) {
        if ((L = httpd_req_recv(R, B.data(), std::min(REM, static_cast<int>(B.size())))) <= 0) { if (L == HTTPD_SOCK_ERR_TIMEOUT) { continue; } ERR = "Rx Failed"; }
        else if (esp_ota_write(H, B.data(), L) != ESP_OK) ERR = "Write Failed";
        else REM -= L;
    }
    if (!ERR && (esp_ota_end(H) != ESP_OK || esp_ota_set_boot_partition(P) != ESP_OK)) ERR = "Finalize Failed";
    if (ERR) { if (H) { esp_ota_abort(H); } N->_Board._Halt.store(false, std::memory_order_relaxed); N->_Log.Warn("OTA: {}", ERR); return httpd_resp_send_err(R, HTTPD_500_INTERNAL_SERVER_ERROR, ERR), ESP_FAIL; }
    httpd_resp_send(R, "OTA Successful", HTTPD_RESP_USE_STRLEN); vTaskDelay(pdMS_TO_TICKS(1000)); esp_restart(); return ESP_OK;
}
