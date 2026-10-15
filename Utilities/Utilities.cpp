#include "Utilities.hpp"
#include <ctime>
#include <cassert>
#include <unistd.h>
#include <cmath>
#include <freertos/task.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/usb_serial_jtag.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_netif_sntp.h"
#include "esp_http_server.h"
#include "driver/temperature_sensor.h"
#include "led_strip.h"

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
        std::time_t Now = std::time(nullptr);
        std::tm T{};
        localtime_r(&Now, &T);
        std::array<char, TIMELEN> TBUF{};
        std::strftime(TBUF.data(), TBUF.size(), "%d/%m/%Y %H:%M:%S", &T);
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
    using namespace Configuration::Hardware;
    temperature_sensor_config_t TCF = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100); esp_err_t E = temperature_sensor_install(&TCF, &_TSH); if (E == ESP_OK) E = temperature_sensor_enable(_TSH); _Report(Device::TEMPERATURE, E != ESP_OK);
    led_strip_config_t SCF{ .strip_gpio_num = GPIO_NUM_48, .max_leds = 1, .led_model = LED_MODEL_WS2812, .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, .timings = {}, .flags = {} }; led_strip_rmt_config_t RCF{ .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10000000, .mem_block_symbols = 0, .flags = {.with_dma = false}};
    esp_err_t RE = led_strip_new_rmt_device(&SCF, &RCF, &_LED);  _Report(Device::RGB, RE != ESP_OK);
}

void Hardware::RGB(uint8_t R, uint8_t G, uint8_t B) { if (_LED) { led_strip_set_pixel(_LED, 0, R, G, B); led_strip_refresh(_LED); }}
void Hardware::BLK(uint8_t N, uint8_t R, uint8_t G, uint8_t B) { for (uint8_t I = 0; I < N; ++I) { RGB(R, G, B); vTaskDelay(pdMS_TO_TICKS(150)); RGB(0, 0, 0); vTaskDelay(pdMS_TO_TICKS(150)); } vTaskDelay(pdMS_TO_TICKS(1000)); }
float Hardware::Temperature() const { float TMP = 0.0f; if (_TSH) temperature_sensor_get_celsius(_TSH, &TMP); return TMP;}
void Hardware::_Report(Device D, bool X) { if (X) _Exceptions |= D; else _Exceptions &= ~D; }

std::string_view Hardware::Summary() {
    char* P = _BUF.data();
    for (size_t I = 0; I < Configuration::Hardware::DEVICES.size(); ++I)
        if (_Exceptions >> I & 1)
            P = std::format_to_n(P, _BUF.data() + _BUF.size() - 1 - P, "{}{}", P == _BUF.data() ? "" : ", ", Configuration::Hardware::DEVICES[I]).out;
    *P = '\0';
    return P == _BUF.data() ? "None" : std::string_view(_BUF.data(), P - _BUF.data());
}

// ------- Power -------
Power::Power(Hardware& B) : _Board(B) {}

void Power::Initialize() {
    using namespace Configuration::Hardware;
    adc_oneshot_unit_init_cfg_t UCF{}; UCF.unit_id = ADC_UNIT_1;
    adc_oneshot_chan_cfg_t ACF{}; ACF.atten = ADC_ATTEN_DB_12; ACF.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_cali_curve_fitting_config_t KCF{}; KCF.unit_id = ADC_UNIT_1; KCF.chan = ADC_CHANNEL_0; KCF.atten = ADC_ATTEN_DB_12; KCF.bitwidth = ADC_BITWIDTH_DEFAULT;
    esp_err_t E = adc_oneshot_new_unit(&UCF, &_ADC); E |= adc_oneshot_config_channel(_ADC, ADC_CHANNEL_0, &ACF); E |= adc_cali_create_scheme_curve_fitting(&KCF, &_CLB);
    vTaskDelay(pdMS_TO_TICKS(10)); uint32_t V = Voltage();
    _Board._Report(Device::BATTERY, E != ESP_OK || V < 9000 || V > 12600);
}

uint32_t Power::Voltage() const {
    int RAW = 0, MV = 0, SUM = 0; using namespace Configuration::Hardware;
    for (uint16_t I = 0; I < BATSAMPLES; ++I) { if (adc_oneshot_read(_ADC, ADC_CHANNEL_0, &RAW) != ESP_OK || adc_cali_raw_to_voltage(_CLB, RAW, &MV) != ESP_OK) return 0; SUM += MV;}
    return SUM * BATNUM / (BATSAMPLES * BATDEN);
}

uint8_t Power::PCT() const { uint32_t V = Voltage(); if (V <= 9000) return 0; if (V >= 12600) return 100; return static_cast<uint8_t>((V - 9000) * 100 / 3600);}

// ------- Drive -------
Drive::Drive(Hardware& B) : _Board(B) {}

void Drive::Initialize() {
    using namespace Configuration::Drive; using namespace Configuration::Hardware;
    uint64_t MSM = 0; for (auto P : DIRS) MSM |= 1ULL << P;
    gpio_config_t DCF{MSM, GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE};
    esp_err_t E = gpio_config(&DCF);
    for (auto P : DIRS) E |= gpio_set_level(P, 0);
    ledc_timer_config_t TCF{}; TCF.speed_mode = LEDC_LOW_SPEED_MODE; TCF.duty_resolution = static_cast<ledc_timer_bit_t>(PWMBITS); TCF.timer_num = LEDC_TIMER_0; TCF.freq_hz = PWMHZ; TCF.clk_cfg = LEDC_AUTO_CLK;
    E |= ledc_timer_config(&TCF);
    for (int I = 0; I < 4; ++I) {
        ledc_channel_config_t CCF{}; CCF.gpio_num = ENS[I]; CCF.speed_mode = LEDC_LOW_SPEED_MODE; CCF.channel = static_cast<ledc_channel_t>(I); CCF.timer_sel = LEDC_TIMER_0;
        E |= ledc_channel_config(&CCF);
    }
    _Board._Report(Device::MOTORS, E != ESP_OK);

    i2c_master_bus_config_t BCF{}; BCF.i2c_port = I2C_NUM_0; BCF.sda_io_num = SDAPIN; BCF.scl_io_num = SCLPIN; BCF.clk_source = I2C_CLK_SRC_DEFAULT; BCF.glitch_ignore_cnt = 7; BCF.flags.enable_internal_pullup = true;
    i2c_device_config_t ICF{I2C_ADDR_BIT_LEN_7, GYROADDR, I2CHZ, 0, {0}};
    bool GYR = i2c_new_master_bus(&BCF, &_SYSBUS) == ESP_OK && i2c_master_bus_add_device(_SYSBUS, &ICF, &_IMU) == ESP_OK;
    if (GYR) {
        for (size_t I = 0; I < GYRO_INIT_CONFIG.size(); I += 2) {
            uint8_t CP[2] = {GYRO_INIT_CONFIG[I], GYRO_INIT_CONFIG[I+1]};
            GYR &= i2c_master_transmit(_IMU, CP, 2, 100) == ESP_OK;
        }
        uint8_t REG = GYRO_WHO_AM_I, ID = 0;
        GYR = GYR && i2c_master_transmit_receive(_IMU, &REG, 1, &ID, 1, 100) == ESP_OK && ID == GYROADDR;
        int32_t SUM = 0; int16_t RW = 0; int N = 0;
        for (int I = 0; GYR && I < BIASN; ++I) { if (_GyroZ(RW)) { SUM += RW; ++N; } vTaskDelay(pdMS_TO_TICKS(10)); }
        GYR = GYR && N > BIASN / 2; _Bias = N ? float(SUM) / float(N) : 0.0f;
    }
    _Board._Report(Device::GYRO, !GYR);

    gpio_config_t GCF{1ULL << TRIGPIN, GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE};
    E = gpio_config(&GCF); E |= gpio_set_level(TRIGPIN, 0);
    GCF.pin_bit_mask = 1ULL << ECHOPIN; GCF.mode = GPIO_MODE_INPUT; GCF.intr_type = GPIO_INTR_ANYEDGE;
    E |= gpio_config(&GCF); E |= gpio_install_isr_service(ESP_INTR_FLAG_IRAM); E |= gpio_isr_handler_add(ECHOPIN, _EchoISR, this);
    _Ping(); vTaskDelay(pdMS_TO_TICKS(POSTMS));
    _Board._Report(Device::USD, E != ESP_OK || !_EchoUs.load(std::memory_order_relaxed));

    if (!(_Board.Exceptions() & (Device::MOTORS | Device::GYRO | Device::USD))) {
        if (xTaskCreatePinnedToCore([](void* P) { static_cast<Drive*>(P)->_Task(); }, "DriveTask", STACK, this, PRIORITY, nullptr, CORE) != pdPASS) _Log.Exception("DriveTask creation failed");
    }
}

bool Drive::_GyroZ(int16_t& RW) {
    constexpr uint8_t REG = Configuration::Hardware::GYRO_Z_REG; std::array<uint8_t, 2> BUF{};
    if (!_IMU || i2c_master_transmit_receive(_IMU, &REG, 1, BUF.data(), 2, 100) != ESP_OK) return false;
    RW = int16_t(BUF[0] << 8 | BUF[1]); return true;
}

void Drive::_Ping() { gpio_set_level(Configuration::Hardware::TRIGPIN, 1); esp_rom_delay_us(10); gpio_set_level(Configuration::Hardware::TRIGPIN, 0); }

IRAM_ATTR void Drive::_EchoISR(void* P) {
    auto* Z = static_cast<Drive*>(P); int64_t NOW = esp_timer_get_time();
    if (gpio_get_level(Configuration::Hardware::ECHOPIN)) Z->_EchoStart = NOW; else Z->_EchoUs.store(uint32_t(NOW - Z->_EchoStart), std::memory_order_relaxed);
}

void Drive::_Task() {
    using namespace Configuration::Drive;
    using namespace Configuration::Hardware;
    TickType_t TIC = xTaskGetTickCount(); Timer TIM; int64_t LST = TIM.Raw(); int PAS = 0;
    float HDG = 0, INT = 0, PRV = 0, PEP = 0, RAT = 0, BAS = 0, SCA = 100, SET = 0; bool LAG = false; int16_t RW = 0;
    while (true) {
        vTaskDelayUntil(&TIC, pdMS_TO_TICKS(TASK_DELAY_MS));
        int64_t NOW = TIM.Raw(); float DT = std::clamp(float(NOW - LST) * 1e-9f, 1e-4f, 0.05f); LST = NOW;
        Action MOD = _Mode.load(std::memory_order_relaxed);
        if (!_GyroZ(RW)) { RAT = 0; if (MOD != Action::STOP) { _Mode.store(Action::STOP, std::memory_order_relaxed); MOD = Action::STOP; _Log.Warn("Gyro read failed"); } }
        else RAT = (RW - _Bias) / GYROSCALE;
        HDG += RAT * DT;
        if (uint16_t REQ = _REQ.exchange(0, std::memory_order_relaxed)) {
            Action NEW = Action((REQ >> 8) - 1); LAG = MOD == Action::STOP && NEW != Action::STOP;
            _Mode.store(NEW, std::memory_order_relaxed); MOD = NEW; BAS = float(REQ & 0xFF); SCA = 100; INT = PRV = 0;
            SET = HDG + (MOD == Action::LEFT ? TURNDEG : MOD == Action::RIGHT ? -TURNDEG : 0);
        }
        float ERR = SET - HDG;
        INT = std::clamp(INT + ERR * DT, -INTEGMAX, INTEGMAX);
        float OUT = HDGKP * ERR + HDGKI * INT + HDGKD * (ERR - PRV) / DT;
        PRV = ERR; int D = int(std::clamp(OUT, -127.0f, 127.0f));
        if (PAS % 10 == 0) {
            uint32_t US = _EchoUs.exchange(0, std::memory_order_relaxed);
            float DIS = std::min(US ? US / US_SOUND_DIVISOR : US_MAX_DISTANCE, US_MAX_DISTANCE), EP = DIS - STANDOFF;
            SCA = std::clamp(PDKP * EP + PDKD * (EP - PEP) / 0.1f, 0.0f, 100.0f); PEP = EP;
            if (DIS < HARDSTOP && MOD == Action::FORWARD) { _Mode.store(Action::STOP, std::memory_order_relaxed); MOD = Action::STOP; _Log.Metrics("Obstacle: [{:.1f}cm]", DIS); }
            _Ping();
        }

        if (PAS == 0 && MOD != Action::STOP) _Log.Metrics("Heading Error: [{:.4f}] | Correction: [{:.4f}]", ERR, float(D));
        PAS = (PAS + 1) % METRICS_INTERVAL_MOD;
        bool LF = true, RF = true; int LS = 0, RS = 0;
        switch (MOD) {
            case Action::FORWARD:
            case Action::REVERSE: {
                bool F = MOD == Action::FORWARD; float B = F ? BAS * SCA * 0.01f : BAS, S = F ? D : -D;
                LF = RF = F; LS = int(std::clamp(B - S, 0.0f, 255.0f)); RS = int(std::clamp(B + S, 0.0f, 255.0f)); break;
            }
            case Action::LEFT:
            case Action::RIGHT:
                if (fabsf(ERR) < TURNTOL) { _Mode.store(Action::STOP, std::memory_order_relaxed); MOD = Action::STOP; }
                else { LF = ERR < 0; RF = ERR > 0; LS = RS = int(BAS); }
                break;
            default: break;
        }
        if (LAG) { RS = 0; LAG = false; }
        for (int I = 0; I < 8; I += 2) { bool FWD = I < 4 ? LF : RF; gpio_set_level(DIRS[I], FWD); gpio_set_level(DIRS[I + 1], !FWD); }
        for (int I = 0; I < 4; ++I) { auto CH = static_cast<ledc_channel_t>(I); ledc_set_duty(LEDC_LOW_SPEED_MODE, CH, I < 2 ? LS : RS); ledc_update_duty(LEDC_LOW_SPEED_MODE, CH); }
    }
}

// ------- Audio -------
Audio::Audio(Hardware& B) : _Board(B) {}

void Audio::Initialize() {
    using namespace Configuration::Audio; using namespace Configuration::Hardware;
    i2s_chan_config_t RCF = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    bool MIC = i2s_new_channel(&RCF, nullptr, &_RX) == ESP_OK;

    if (MIC) {
        i2s_std_gpio_config_t GCF{}; GCF.mclk = I2S_GPIO_UNUSED; GCF.bclk = MICPINS[0]; GCF.ws = MICPINS[1]; GCF.dout = I2S_GPIO_UNUSED; GCF.din = MICPINS[2];
        i2s_std_config_t SCF{ .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MICHZ), .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), .gpio_cfg = GCF };
        MIC = i2s_channel_init_std_mode(_RX, &SCF) == ESP_OK && i2s_channel_enable(_RX) == ESP_OK;
    }

    size_t RB = 0; bool MOK = MIC && i2s_channel_read(_RX, _BUF.data(), FRAME * sizeof(int16_t), &RB, pdMS_TO_TICKS(MICMS)) == ESP_OK && RB == (FRAME * sizeof(int16_t));
    _Board._Report(Device::MIC, !MOK);

    i2s_chan_config_t TCF = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    bool SPK = i2s_new_channel(&TCF, &_TX, nullptr) == ESP_OK;
    if (SPK) { i2s_std_gpio_config_t GCF{}; GCF.mclk = I2S_GPIO_UNUSED; GCF.bclk = SPKPINS[0]; GCF.ws = SPKPINS[1]; GCF.dout = SPKPINS[2]; GCF.din = I2S_GPIO_UNUSED;
        i2s_std_config_t SCF{ .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPKHZ), .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), .gpio_cfg = GCF };
        SPK = i2s_channel_init_std_mode(_TX, &SCF) == ESP_OK && i2s_channel_enable(_TX) == ESP_OK; }
    _Board._Report(Device::SPEAKER, !SPK);

    if (!_Board.Exception(Device::MIC)) {
        if (xTaskCreatePinnedToCore([](void* P) { static_cast<Audio*>(P)->_Task(); }, "AudioTask", STACK, this, PRIORITY, nullptr, CORE) != pdPASS) _Log.Exception("AudioTask creation failed");
    } else {
        _Log.Exception("Audio Initialization Failed");
    }
}

// ------- Audio -------
void Audio::_Task() {
    using namespace Configuration::Audio;
    TickType_t TIC = xTaskGetTickCount(); bool LST = false; Timer LAT;
    while (true) {
        vTaskDelayUntil(&TIC, pdMS_TO_TICKS(32));
        i2s_channel_disable(_RX); i2s_channel_enable(_RX);
        size_t RB = 0;
        if (i2s_channel_read(_RX, _BUF.data(), FRAME * sizeof(int16_t), &RB, 0) != ESP_OK || RB == 0) { vTaskDelay(1); continue; }
        if (!LST) {
            // WakeNet detection
        } else {
            // MultiNet detection
        }
    }
}

// ------- UI -------
UI::UI(Hardware& B, Drive& M, Audio& A) : _Board(B), _Motors(M), _AU(A) {}

void UI::Initialize() {
    using namespace Configuration::UI; using namespace Configuration::Hardware;

    spi_bus_config_t BCF{}; BCF.sclk_io_num = TFTPINS[0]; BCF.mosi_io_num = TFTPINS[1]; BCF.miso_io_num = -1; BCF.quadwp_io_num = -1; BCF.quadhd_io_num = -1; BCF.max_transfer_sz = sizeof(_Strip);
    spi_device_interface_config_t DCF{}; DCF.clock_speed_hz = SPIHZ; DCF.spics_io_num = TFTPINS[3]; DCF.queue_size = 1;
    gpio_config_t GCF{(1ULL << TFTPINS[2]) | (1ULL << TFTPINS[4]), GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE};
    esp_err_t E = spi_bus_initialize(SPI2_HOST, &BCF, SPI_DMA_CH_AUTO);
    E |= spi_bus_add_device(SPI2_HOST, &DCF, &_TFT); E |= gpio_config(&GCF);
    if (E == ESP_OK) {
        gpio_set_level(TFTPINS[4], 0); vTaskDelay(pdMS_TO_TICKS(TFTDELA)); gpio_set_level(TFTPINS[4], 1); vTaskDelay(pdMS_TO_TICKS(TFTDELB));
        for (size_t I = 0, N = 0; TFTINIT[I]; I += 2 + N) { N = TFTINIT[I + 1] & 0x7F; E |= _Send(TFTINIT[I], &TFTINIT[I + 2], N); if (TFTINIT[I + 1] & 0x80) vTaskDelay(pdMS_TO_TICKS(TFTIDEL)); }
        E |= _Send(CASET, FULL.data(), FULL.size()); E |= _Send(RASET, FULL.data(), FULL.size());
        for (uint8_t I = 0; I < HEIGHT / STRIPROWS; ++I) E |= _Send(I ? RAMWRC : RAMWR, reinterpret_cast<const uint8_t*>(_Strip.data()), sizeof(_Strip));
    }
    _Board._Report(Device::TFT, E != ESP_OK);

    usb_serial_jtag_driver_config_t SCF = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    _Board._Report(Device::SERIAL, usb_serial_jtag_driver_install(&SCF) != ESP_OK);

    if (xTaskCreatePinnedToCore([](void* P) { static_cast<UI*>(P)->_Task(); }, "UITask", STACK, this, PRIORITY, nullptr, CORE) != pdPASS) _Log.Exception("UITask creation failed");
}

void UI::_Run(Action A) { if (A == Action::STATUS) _Log.Metrics("{} - {}%", Configuration::Actions::NAMES[int(_Motors.CMode())], _BPT.load()); else _Motors.Act(A); }

void UI::_Task() {
    using namespace Configuration::UI;
    uint8_t LEN = 0; uint8_t C = 0;
    std::array<char, 16> LIN{};
    const TickType_t delayTicks = pdMS_TO_TICKS(1000);
    while (true) {
        vTaskDelay(delayTicks);
        while (!_Board.Exception(Hardware::Device::SERIAL) && usb_serial_jtag_read_bytes(&C, 1, 0) > 0) {
            if (C != '\n' && C != '\r') { if (LEN < LIN.size()) LIN[LEN++] = char(C); continue; }
            for (uint8_t I = 0; I < Configuration::Actions::NAMES.size(); ++I) { if (std::string_view(LIN.data(), LEN) == Configuration::Actions::NAMES[I]) { _Run(Action(I)); }} LEN = 0;
        }
    }
}

esp_err_t UI::_Send(uint8_t CMD, const uint8_t* DAT, size_t LEN) {
    spi_transaction_t TX{}; TX.length = 8; TX.tx_buffer = &CMD;
    gpio_set_level(Configuration::Hardware::TFTPINS[2], 0);
    esp_err_t E = spi_device_polling_transmit(_TFT, &TX);
    if (LEN) { TX.length = LEN * 8; TX.tx_buffer = DAT; gpio_set_level(Configuration::Hardware::TFTPINS[2], 1); E |= spi_device_polling_transmit(_TFT, &TX); }
    return E;
}

std::string_view UI::EXPR() const { using namespace Configuration::UI; return NAMES[static_cast<size_t>(_Expression)];}

// ------- Network -------
extern "C" const char _binary_Status_html_start;
extern "C" const char _binary_Status_html_end;

Network::Network(Hardware& B, Power& P, UI& U) : _Board(B), _Battery(P), _UI(U) {}

void Network::_Reconnect(void*, esp_event_base_t, int32_t, void*) { esp_wifi_connect(); }

void Network::Initialize() {
    using namespace Configuration::Network; using namespace Configuration::Hardware;
    wifi_init_config_t INIT = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t WCFG{};
    strncpy(reinterpret_cast<char*>(WCFG.sta.ssid), SSID.data(), sizeof(WCFG.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(WCFG.sta.password), PASS.data(), sizeof(WCFG.sta.password) - 1);
    WCFG.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t W = esp_netif_init() | esp_event_loop_create_default();
    esp_netif_t* NET = esp_netif_create_default_wifi_sta();
    W |= esp_wifi_init(&INIT) | esp_wifi_set_mode(WIFI_MODE_STA) | esp_wifi_set_config(WIFI_IF_STA, &WCFG) | esp_wifi_start();
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, _Reconnect, nullptr);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, _Reconnect, nullptr);
    vTaskDelay(pdMS_TO_TICKS(WIFISETTLEMS)); esp_wifi_connect();

    esp_netif_ip_info_t IP{};
    for (uint16_t I = 0; !IP.ip.addr && I < WIFIMS / 50; ++I) { vTaskDelay(pdMS_TO_TICKS(50)); esp_netif_get_ip_info(NET, &IP); }
    esp_netif_dns_info_t DNS{}; DNS.ip.type = IPADDR_TYPE_V4; DNS.ip.u_addr.ip4.addr = ipaddr_addr(DDNS.data()); esp_netif_set_dns_info(NET, ESP_NETIF_DNS_MAIN, &DNS);

    esp_sntp_config_t NTP = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTPHOST.data()); esp_netif_sntp_deinit();
    bool SYNC = IP.ip.addr && esp_netif_sntp_init(&NTP) == ESP_OK && esp_netif_sntp_sync_wait(pdMS_TO_TICKS(NTPMS)) == ESP_OK;
    setenv("TZ", TZONE.data(), 1); tzset();

    std::array<uint8_t, 6> MACADR; esp_netif_get_mac(NET, MACADR.data());
    _Board._Report(Device::WIFI, W != ESP_OK || IP.ip.addr == 0); _Board._Report(Device::CLOCK, !SYNC);
    _Log.Metrics("IP: [{}.{}.{}.{}] | MAC: [{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}]",
        IP.ip.addr & 255, (IP.ip.addr >> 8) & 255, (IP.ip.addr >> 16) & 255, IP.ip.addr >> 24, MACADR[0], MACADR[1], MACADR[2], MACADR[3], MACADR[4], MACADR[5]);

    if (!_Board.Exception(Device::WIFI)) {
        httpd_config_t HCF = HTTPD_DEFAULT_CONFIG(); HCF.core_id = Configuration::UI::CORE; HCF.uri_match_fn = httpd_uri_match_wildcard; httpd_handle_t SRV = nullptr;
        httpd_uri_t URI{.uri = URIROOT.data(), .method = HTTP_GET, .handler = _HTML, .user_ctx = this}, LURI{.uri = URILOGS.data(), .method = HTTP_GET, .handler = _Logs, .user_ctx = this}, TURI{.uri = URITELEMETRY.data(), .method = HTTP_GET, .handler = _Telemetry, .user_ctx = this};
        if (httpd_start(&SRV, &HCF) != ESP_OK || httpd_register_uri_handler(SRV, &URI) != ESP_OK || httpd_register_uri_handler(SRV, &LURI) != ESP_OK || httpd_register_uri_handler(SRV, &TURI) != ESP_OK) _Log.Exception("HTTP server failed");
    }
}

esp_err_t Network::_HTML(httpd_req_t* REQ) {
    std::string_view PAGE(&_binary_Status_html_start, &_binary_Status_html_end - &_binary_Status_html_start);
    httpd_resp_set_type(REQ, "text/html");
    return httpd_resp_send(REQ, PAGE.data(), PAGE.size());
}

esp_err_t Network::_Logs(httpd_req_t* R) {
    std::string P = "/littlefs" + std::string(R->uri); FILE* F = std::fopen(P.c_str(), "rb");
    if (!F) return httpd_resp_send_err(R, HTTPD_404_NOT_FOUND, "Not found"), ESP_FAIL;
    httpd_resp_set_type(R, "text/plain"); char B[256]; size_t L;
    while ((L = std::fread(B, 1, sizeof(B), F)) > 0) httpd_resp_send_chunk(R, B, L);
    std::fclose(F); httpd_resp_send_chunk(R, nullptr, 0); return ESP_OK;
}

esp_err_t Network::_Telemetry(httpd_req_t* R) {
    wifi_ap_record_t AP{}; int RSSI = (esp_wifi_sta_get_ap_info(&AP) == ESP_OK) ? AP.rssi : 0;
    auto* N = static_cast<Network*>(R->user_ctx);

    std::array<char, 256> B{};
    auto RES = std::format_to_n(B.data(), B.size(), R"({{"BPCT":{},"BVLT":{:.2f},"Temperature":{:.1f},"Expression":"{}","RSSI":{},"Exceptions":"{}"}})", N->_Battery.PCT(), static_cast<float>(N->_Battery.Voltage()) / 1000.0f, N->_Board.Temperature(), N->_UI.EXPR(), RSSI, N->_Board.Summary());

    size_t LEN = static_cast<size_t>(RES.out - B.data());
    httpd_resp_set_type(R, "application/json");
    return httpd_resp_send(R, B.data(), static_cast<ssize_t>(LEN));
}
