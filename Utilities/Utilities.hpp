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
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "driver/temperature_sensor.h"
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Configuration.hpp"
#include <esp_ota_ops.h>
#include "led_strip.h" // IWYU pragma: keep

// ------- Logger -------
class Logger {
public:
    using LF = Configuration::Logger::LF;
    using LVL = Configuration::Logger::LVL;
    Logger(const char* M, LF F);

    template<class... A> void Trace(std::format_string<A...> F, A&&... ARGS) const { _Log(LVL::TRACE, F, std::forward<A>(ARGS)...); }
    template<class... A> void Metrics(std::format_string<A...> F, A&&... ARGS) const { _Log(LVL::METRICS, F, std::forward<A>(ARGS)...); }
    template<class... A> void Warn(std::format_string<A...> F, A&&... ARGS) const { _Log(LVL::WARN, F, std::forward<A>(ARGS)...); }
    template<class... A> void Exception(std::format_string<A...> F, A&&... ARGS) const { _Log(LVL::EXCEPTION, F, std::forward<A>(ARGS)...); }
private:
    struct Entry { const char* M; LF F; LVL L; std::array<char, Configuration::Logger::MSGLEN> MSG; };

    template<class... A> void _Log(LVL L, std::format_string<A...> F, A&&... ARGS) const {
        Entry I{_M, _F, L, {}};
        *std::format_to_n(I.MSG.data(), I.MSG.size() - 1, F, std::forward<A>(ARGS)...).out = '\0';
        xQueueSend(_Queue, &I, 0);
    }

    static void _ProcessQueue(void* P);
    const char* _M;
    LF _F;
    inline static QueueHandle_t _Queue{};
    inline static std::array<int, Configuration::Logger::PATHS.size()> _Files{-1, -1};
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
    using Device = Configuration::Hardware::Device; using Expression = Configuration::Hardware::Expression; using Action = Configuration::Actions::Action;
    bool Exception(Device D) const { return _Exceptions & D; }
    uint16_t Exceptions() const { return _Exceptions; }
    void Initialize();
    float Temperature() const { float T = 0.0f; if (_TSH) temperature_sensor_get_celsius(_TSH, &T); return T; }
    void RGB(uint8_t R, uint8_t G, uint8_t B) { if (_LED) { led_strip_set_pixel(_LED, 0, R, G, B); led_strip_refresh(_LED); } }
    void BLK(uint8_t N, uint8_t R, uint8_t G, uint8_t B) { for (uint8_t I = 0; I < N; ++I) { RGB(R, G, B); vTaskDelay(pdMS_TO_TICKS(150)); RGB(0, 0, 0); vTaskDelay(pdMS_TO_TICKS(150)); } vTaskDelay(pdMS_TO_TICKS(1000)); }
    float Distance() const { return _Distance.load(std::memory_order_relaxed); }
    std::string_view Summary();
    i2c_master_bus_handle_t I2C() const { return _I2C; }
    spi_host_device_t SPI() const { return SPI2_HOST; }
    uint32_t Voltage() const;
    static uint8_t Charge(uint32_t MV) { using namespace Configuration::Hardware; return MV <= BATMIN ? 0 : MV >= BATMAX ? 100 : (MV - BATMIN) / BATSPAN; }
    uint8_t PCT() const { return _Pct.load(std::memory_order_relaxed); }
    uint16_t MV() const { return _MV.load(std::memory_order_relaxed); }
    bool Halted() const { return _Halt.load(std::memory_order_relaxed); }
    Action Mode() const { return _Mode.load(std::memory_order_relaxed); }
    void SetExpression(Expression Expr) { _Expression.store(Expr, std::memory_order_relaxed); }

    std::string_view EXPR() const { size_t IDX = static_cast<size_t>(_Expression.load(std::memory_order_relaxed)); return ::Configuration::Hardware::NAMES[IDX]; }

private:
    friend class Drive; friend class UI; friend class Network; friend class Orchestrator;
    void _Report(Device D, bool X) { if (X) _Exceptions |= D; else _Exceptions &= ~D; }
    uint16_t _Exceptions{};
    std::atomic<float> _Distance{0.0f};
    std::atomic<Expression> _Expression{Expression::DEAD};
    std::atomic<Action> _Mode{Action::STOP};
    std::atomic<bool> _Halt{false};
    std::atomic<uint8_t> _Pct{0};
    std::atomic<uint16_t> _MV{0};
    std::array<char, Configuration::Logger::MSGLEN> _BUF{};
    Logger _Log{"HARDWARE", Configuration::Logger::LF::HARDWARE};
    temperature_sensor_handle_t _TSH{nullptr};
    led_strip_handle_t _LED{nullptr};
    i2c_master_bus_handle_t _I2C{nullptr};
    adc_oneshot_unit_handle_t _ADC{nullptr};
    adc_cali_handle_t _CLB{nullptr};
};

// ------- Drive -------
class Drive {
public:
    using Action = Configuration::Actions::Action;
    explicit Drive(Hardware& B) : _Board(B) {}
    void Initialize();

private:
    void _Task();
    void _Ping();
    void _Motors(bool LF, bool RF, int LS, int RS);
    IRAM_ATTR static void _EchoISR(void* P);

    std::atomic<uint32_t> _EchoUs{};
    int64_t _EchoStart{};
    float _Bias{};
    i2c_master_dev_handle_t _IMU{};
    Hardware& _Board;
    Logger _Log{"DRIVE", Configuration::Logger::LF::HARDWARE};
};

// ------- UI -------
class UI {
public:
    explicit UI(Hardware& Board) : _Board(Board) {}
    void Initialize();

private:
    void _Task();
    esp_err_t _Display(uint8_t CMD, const uint8_t* DAT, size_t LEN);
    std::array<uint16_t, Configuration::UI::WIDTH * Configuration::UI::STRIPROWS> _Strip{};
    spi_device_handle_t _TFT{};
    Hardware& _Board;
    Logger _Log{"UI", Configuration::Logger::LF::ORCHESTRATOR};
};

// ------- Network -------
class Network {
public:
    explicit Network(Hardware& Board) : _Board(Board) {}
    void Initialize();
private:
    void _Stop();
    static void _Reconnect(void* Arg, esp_event_base_t Base, int32_t ID, void* Data);
    static esp_err_t _HTML(httpd_req_t* REQ);
    static esp_err_t _Logs(httpd_req_t* REQ);
    static esp_err_t _Telemetry(httpd_req_t* R);
    static esp_err_t _Restart(httpd_req_t* REQ);
    static esp_err_t _Shutdown(httpd_req_t* REQ);
    static esp_err_t _OTA(httpd_req_t* REQ);
    Hardware& _Board;
    Logger _Log{"NETWORK", Configuration::Logger::LF::ORCHESTRATOR};
};
