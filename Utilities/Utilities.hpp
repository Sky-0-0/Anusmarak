#pragma once
#include <array>
#include <format>
#include <atomic>
#include <cstdio>
#include <utility>
#include <string_view>
#include <cstdint>
#include <esp_attr.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <driver/i2s_std.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_mn_iface.h>
#include <esp_http_server.h>
#include "Configuration.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_adc/adc_cali_scheme.h>
#include "driver/temperature_sensor.h"
#include "led_strip.h" // IWYU pragma: keep

// ------- Logger -------
class Logger {
public:
    using LF = Configuration::Logger::LF;
    using LVL = Configuration::Logger::LVL;
    Logger(const char* M, LF F);

    template<class... A> void Trace(std::string_view F, A&&... ARGS) const { _Log(LVL::TRACE, F, std::forward<A>(ARGS)...); }
    template<class... A> void Metrics(std::string_view F, A&&... ARGS) const { _Log(LVL::METRICS, F, std::forward<A>(ARGS)...); }
    template<class... A> void Warn(std::string_view F, A&&... ARGS) const { _Log(LVL::WARN, F, std::forward<A>(ARGS)...); }
    template<class... A> void Exception(std::string_view F, A&&... ARGS) const { _Log(LVL::EXCEPTION, F, std::forward<A>(ARGS)...); }
private:
    struct Entry { const char* M; LF F; LVL L; std::array<char, Configuration::Logger::MSGLEN> MSG; };

    template<class... A> void _Log(LVL L, std::string_view F, A&&... ARGS) const {
        Entry I{_M, _F, L, {}};
        auto Res = std::vformat_to(I.MSG.data(), F, std::make_format_args(ARGS...));
        *Res = '\0';
        xQueueSend(_Queue, &I, 0);
    }

    static void _ProcessQueue(void* P);
    const char* _M;
    LF _F;
    inline static QueueHandle_t _Queue{};
};

// ------- Timer -------
class Timer {
public:
    Timer() { Start(); }
    void Start() { _T0 = _Now(); }
    int64_t Raw() const { return _Now() - _T0; }
    std::string_view Elapsed() { return _F(Raw()); }
private:
    static int64_t _Now() { return esp_timer_get_time() * 1000; }
    int64_t _T0;
    std::array<char, Configuration::Timer::BUFLEN> _BUF{};
    std::string_view _F(int64_t NS);
};

// ------- Hardware -------
class Hardware {
public:
    using Device = Configuration::Hardware::Device;
    bool Exception(Device D) const { return _Exceptions & D; }
    uint16_t Exceptions() const { return _Exceptions; }
    void Initialize();
    float Temperature() const;
    void RGB(uint8_t R, uint8_t G, uint8_t B);
    void BLK(uint8_t N, uint8_t R, uint8_t G, uint8_t B);
    std::string_view Summary();

private:
    friend class Drive; friend class UI; friend class Audio; friend class BootLoader; friend class Network; friend class Power;
    void _Report(Device D, bool Exception);
    uint16_t _Exceptions{};
    std::array<char, Configuration::Logger::MSGLEN> _BUF{};
    Logger _Log{"HARDWARE", Configuration::Logger::LF::HARDWARE};

    temperature_sensor_handle_t _TSH{nullptr};
    led_strip_handle_t _LED{nullptr};
};

// ------- Drive -------
class Drive {
public:
    using Action = Configuration::Actions::Action;
    Drive(Hardware& B);
    void Initialize();
    void Act(Action A, uint8_t S = Configuration::Drive::DEFAULTSPEED) { _REQ.store(uint16_t((A + 1) << 8 | S), std::memory_order_relaxed); }
    Action CMode() const { return _Mode.load(std::memory_order_relaxed); }
private:
    void _Task();
    void _Ping();
    bool _GyroZ(int16_t& RW);
    IRAM_ATTR static void _EchoISR(void* P);
    std::atomic<uint16_t> _REQ{};
    std::atomic<Action> _Mode{Action::STOP};
    std::atomic<uint32_t> _EchoUs{};
    int64_t _EchoStart{};
    float _Bias{};
    i2c_master_bus_handle_t _SYSBUS{};
    i2c_master_dev_handle_t _IMU{};
    Hardware& _Board;
    Logger _Log{"DRIVE", Configuration::Logger::LF::HARDWARE};
};

// ------- Audio -------
class Audio {
public:
    Audio(Hardware& Board);
    void Initialize();
private:
    void _Task();
    esp_wn_iface_t* _WakeNet{};
    esp_mn_iface_t* _MultiNet{};
    model_iface_data_t* _WND{};
    model_iface_data_t* _MND{};
    std::array<int16_t, Configuration::Audio::BUFLEN> _BUF{};
    i2s_chan_handle_t _RX{}, _TX{};
    Hardware& _Board;
    Logger _Log{"AUDIO", Configuration::Logger::LF::AUDIO};
};

// ------- Power -------
class Power {
public:
    explicit Power(Hardware& B);
    void Initialize();
    uint32_t Voltage() const;
    uint8_t PCT() const;

private:
    adc_oneshot_unit_handle_t _ADC{nullptr};
    adc_cali_handle_t _CLB{nullptr};
    Hardware& _Board;
};

// ------- UI -------
class UI {
public:
    using Action = Configuration::Actions::Action;
    using Expression = Configuration::UI::Expression;
    UI(Hardware& Board, Drive& Motors, Audio& AU);
    void Initialize();
    std::string_view EXPR() const;
private:
    void _Task();
    void _Run(Action A);
    bool _Draw(uint8_t First, uint8_t Last);
    esp_err_t _Send(uint8_t CMD, const uint8_t* DAT, size_t LEN);
    uint8_t _Blink{};
    std::array<uint8_t, 4> _Shown{0xFF, 0xFF, 0xFF, 0xFF};
    alignas(4) std::array<uint16_t, Configuration::UI::WIDTH * Configuration::UI::STRIPROWS> _Strip{};
    std::atomic<uint8_t> _BPT{};
    spi_device_handle_t _TFT{};
    Hardware& _Board;
    Drive& _Motors;
    Audio& _AU;
    Logger _Log{"UI", Configuration::Logger::LF::UTILITIES};

    Configuration::UI::Expression _Expression{Configuration::UI::Expression::DEAD};
};

// ------- Network -------
class Network {
public:
    Network(Hardware& Board, Power& Battery, UI& UI);
    void Initialize();
private:
    static void _Reconnect(void* Arg, esp_event_base_t Base, int32_t ID, void* Data);
    static esp_err_t _HTML(httpd_req_t* REQ);
    static esp_err_t _Logs(httpd_req_t* REQ);
    static esp_err_t _Telemetry(httpd_req_t* R);
    Hardware& _Board; Power& _Battery; UI& _UI;
    Logger _Log{"Network", Configuration::Logger::LF::UTILITIES};
};
