#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <string_view>
#include <utility>
#include <driver/gpio.h>

namespace Configuration {

    // ------- Logger -------
    namespace Logger {
        enum LF : uint8_t { ORCHESTRATOR, HARDWARE };
        enum LVL : uint8_t { TRACE, METRICS, WARN, EXCEPTION };
        inline constexpr std::array<std::string_view, 2> PATHS{ "/littlefs/Logs/Orchestrator.log", "/littlefs/Logs/Hardware.log" };
        inline constexpr std::array<std::string_view, 4> LEVELS{ "Trace", "Metrics", "Warn", "Exception" };
        inline constexpr uint8_t DEPTH{16}, MSGLEN{82}, TIMELEN{20}, PRIORITY{3}, CORE{1};
        inline constexpr uint16_t BUFLEN{136}, STACK{4096};
    }

    // ------- Timer -------
    namespace Timer {
        inline constexpr uint8_t BUFLEN{16};
        inline constexpr int64_t TH{3600000000000LL}, TM{60000000000LL}, TS{1000000000LL}, TMS{1000000LL};
    }

    // ------- Hardware -------
    namespace Hardware {
        enum Device : uint16_t { TEMPERATURE = 1 << 0, RGB = 1 << 1, BATTERY = 1 << 2, MOTORS = 1 << 3, IMU = 1 << 4, USD = 1 << 5, TFT = 1 << 6, SERIAL = 1 << 7, WIFI = 1 << 8 };
        inline constexpr std::array<std::string_view, 9> DEVICES{ "TEMPERATURE", "RGB", "BATTERY", "MOTORS", "IMU", "USD", "TFT", "SERIAL", "WIFI" };
        enum Expression : uint8_t { HAPPY, WORRIED, SLEEPY, DEAD };
        inline constexpr std::array<std::string_view, 4> NAMES{ "HAPPY", "WORRIED", "SLEEPY", "DEAD"};
        inline constexpr std::array<gpio_num_t, 8> DIRS{GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_12, GPIO_NUM_13}; // Module 1 (left): IN1, IN2 = A front, IN3, IN4 = C rear | Module 2 (right): IN1, IN2 = B front, IN3, IN4 = D rear
        inline constexpr std::array<gpio_num_t, 4> ENS{GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_38}; // Module 1: ENA = A, ENB = C | Module 2: ENA = B, ENB = D
        inline constexpr std::array<gpio_num_t, 6> TFTPINS{GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_47, GPIO_NUM_21, GPIO_NUM_41, GPIO_NUM_40}; // TFT:SCL, TFT:SDA, TFT:RES, TFT:DC, TFT:CS, TFT:BLK
        inline constexpr gpio_num_t TRIGPIN{GPIO_NUM_42}, ECHOPIN{GPIO_NUM_3}, SCLPIN{GPIO_NUM_10}, SDAPIN{GPIO_NUM_11}; // USD:Trig, USD:Echo, IMU:SCL, IMU:SDA
        inline constexpr uint16_t BIASN{200}, POSTMS{60}, BATSAMPLES{8}, BATNUM{49}, BATDEN{10}, BATMIN{9000}, BATSPAN{36}, USGLITCH{100};
        inline constexpr uint16_t BATMAX{BATMIN + 100 * BATSPAN};
        inline constexpr uint32_t I2CHZ{400000}, SPIHZ{40000000}, PWMHZ{20000};
        inline constexpr uint8_t PWMBITS{8}, IMUADDR{0x68}, IMUZREG{0x47};
        inline constexpr float IMUSCALE{65.5f};
        inline constexpr uint8_t IMUREG{0x75}, IMUEXPECTEDID{0X70};
        inline constexpr std::array<uint8_t, 6> IMUINIT{0x6B, 0x00, 0x1A, 0x03, 0x1B, 0x08}; // PWR_MGMT_1 wake, DLPF 44Hz, IMU_CONFIG ±500°/s
        inline constexpr float USDIV{58.0f};
        inline constexpr float USMAX{400.0f};
    }

    // ------- Network -------
    namespace Network {
        inline constexpr std::string_view SSID{"WIN-7CBRB18PEP0 5553"};
        inline constexpr std::string_view PASS{"e8&761M28"};
        inline constexpr std::string_view URIROOT{"/"};
        inline constexpr std::string_view URILOGS{"/Logs/*"};
        inline constexpr std::string_view URITELEMETRY{"/Telemetry"};
        inline constexpr uint8_t PRIORITY{4}, CORE{1};
        inline constexpr uint16_t HALTMS{100};
    }

    // ------- Drive -------
    namespace Drive {
        inline constexpr uint8_t DEFAULTSPEED{128}, MINPWM{90}, ESCAPEPWM{128}, PRIORITY{10}, CORE{0}; // MINPWM and ESCAPEPWM are provisional until measured on the expo floor
        inline constexpr uint16_t STACK{4096};
        inline constexpr float HDGKP{1.5f}, HDGKI{0.02f}, HDGKD{0.8f}, INTEGMAX{2000.0f};
        inline constexpr float PDKP{2.0f}, PDKD{0.5f}, STANDOFF{20.0f}, TURNDEG{90.0f}, TURNTOL{2.0f};
        inline constexpr uint32_t TASKMS{10}, TURNTMS{3000};
        inline constexpr uint16_t TURNTMO{TURNTMS / TASKMS};
        inline constexpr uint8_t PASSWRAP{100}, SETTLEPASS{Hardware::POSTMS / TASKMS};
    }

    // ------- Actions -------
    namespace Actions {
        enum Action : uint8_t { STOP, FORWARD, LEFT, RIGHT };
    }

    // ------- UI (GC9A01 240x240 Round TFT) -------
        namespace UI {
            inline constexpr uint16_t WIDTH{240}, HEIGHT{240}, CENTERX{120}, CENTERY{120}, REFRESHRATE{40}, STACK{4096};
            inline constexpr uint8_t STRIPROWS{8}, PRIORITY{5}, CORE{1}, EYEDX{50}, EYEW{30}, EYEH{40}, BLINKFRAMES{6};
            inline constexpr uint8_t EYESTARTSTRIP{(CENTERY - 35) / STRIPROWS}, EYEENDSTRIP{(CENTERY + 35) / STRIPROWS};
            inline constexpr uint16_t RINGIN{110}, RINGOUT{114}; // RGB565
            inline constexpr std::array<uint16_t, 4> EYEHEIGHTS{40, 20, 16, 4}; // HAPPY, WORRIED, SLEEPY, DEAD
            inline constexpr std::array<uint16_t, 4> EYECOLORS{0x96FF, 0x96FF, 0x96FF, 0x07E0}; // HAPPY, WORRIED, SLEEPY, DEAD
            inline constexpr std::array<uint16_t, 4> RINGCOLORS{0x3186, 0xBD45, 0xCB63, 0xB945}; // green, yellow, orange, red
            inline constexpr std::array<uint8_t, 3> RINGLIMITS{60, 35, 15};
            inline constexpr uint8_t CASET{0x2A}, RASET{0x2B}, RAMWR{0x2C}, RAMWRC{0x3C};
            inline constexpr std::array<uint8_t, 4> FULL{0, 0, 0, WIDTH - 1};
            inline constexpr std::array<uint8_t, 4> EYECOLS{0, CENTERX - EYEDX - EYEW / 2, 0, CENTERX + EYEDX + EYEW / 2};
            inline constexpr std::array<uint8_t, 4> EYEROWS{0, EYESTARTSTRIP * STRIPROWS, 0, (EYEENDSTRIP + 1) * STRIPROWS - 1};
            inline constexpr uint32_t TFTDELA{10}, TFTDELB{120}, TFTIDEL{150};
            inline constexpr std::array<uint8_t, 224> TFTINIT{
                0xEF,0, 0xEB,1,0x14, 0xFE,0, 0xEF,0, 0xEB,1,0x14,
                0x84,1,0x40, 0x85,1,0xFF, 0x86,1,0xFF, 0x87,1,0xFF, 0x88,1,0x0A, 0x89,1,0x21, 0x8A,1,0x00, 0x8B,1,0x80, 0x8C,1,0x01, 0x8D,1,0x01, 0x8E,1,0xFF, 0x8F,1,0xFF,
                0xB6,2,0x00,0x00, 0x36,1,0x48, 0x3A,1,0x05, 0x90,4,0x08,0x08,0x08,0x08,
                0xBD,1,0x06, 0xBC,1,0x00, 0xFF,3,0x60,0x01,0x04, 0xC3,1,0x13, 0xC4,1,0x13, 0xC9,1,0x22, 0xBE,1,0x11,
                0xE1,2,0x10,0x0E, 0xDF,3,0x21,0x0C,0x02,
                0xF0,6,0x45,0x09,0x08,0x08,0x26,0x2A, 0xF1,6,0x43,0x70,0x72,0x36,0x37,0x6F,
                0xF2,6,0x45,0x09,0x08,0x08,0x26,0x2A, 0xF3,6,0x43,0x70,0x72,0x36,0x37,0x6F,
                0xED,2,0x1B,0x0B, 0xAE,1,0x77, 0xCD,1,0x63, 0xE8,1,0x34,
                0x62,12,0x18,0x0D,0x71,0xED,0x70,0x70,0x18,0x0F,0x71,0xEF,0x70,0x70,
                0x63,12,0x18,0x11,0x71,0xF1,0x70,0x70,0x18,0x13,0x71,0xF3,0x70,0x70,
                0x64,7,0x28,0x29,0xF1,0x01,0xF1,0x00,0x07, 0x66,10,0x3C,0x00,0xCD,0x67,0x45,0x45,0x10,0x00,0x00,0x00,
                0x67,10,0x00,0x3C,0x00,0x00,0x00,0x01,0x54,0x10,0x32,0x98, 0x74,7,0x10,0x85,0x80,0x00,0x00,0x4E,0x00, 0x98,2,0x3E,0x07,
                0x35,0, 0x21,0, 0x11,0x80, 0x29,0x80, 0x00
            };
        }

    // ------- Orchestrator -------
    namespace Orchestrator {
        inline constexpr uint8_t PRIORITY{3}, CORE{1}, SAMPLES{5}, SAMPLEMOD{10}, JUMPPCT{10}, SLEEPYIN{UI::RINGLIMITS[2]}, SLEEPYOUT{20};
        inline constexpr uint16_t STACK{4096}, TICKMS{100}, SPREADMV{100};
        inline constexpr float WORRYON{Drive::STANDOFF + 10.0f}, WORRYOFF{Drive::STANDOFF + 15.0f};
    }
}
