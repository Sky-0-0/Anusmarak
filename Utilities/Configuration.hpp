#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <string_view>
#include <utility>
#include <driver/gpio.h>

inline constexpr int SUCCESS{0}, FAILURE{1};

namespace Configuration {

    // ------- Logger -------
    namespace Logger {
        enum LF : uint8_t { UTILITIES, HARDWARE, AUDIO };
        enum LVL : uint8_t { TRACE, METRICS, WARN, EXCEPTION };
        inline constexpr std::array<std::string_view, 3> PATHS{ "/littlefs/Logs/Utilities.log", "/littlefs/Logs/Hardware.log", "/littlefs/Logs/Audio.log" };
        inline constexpr std::array<std::string_view, 4> LEVELS{ "Trace", "Metrics", "Warn", "Exception" };
        inline constexpr uint8_t DEPTH{16}, MSGLEN{82}, TIMELEN{20}, PRIORITY{3}, CORE{1};
        inline constexpr uint16_t BUFLEN{136}, STACK{4096};
        [[maybe_unused]] inline static std::array<int, Configuration::Logger::PATHS.size()> _Files{-1, -1, -1};
        static_assert(PATHS.size() == AUDIO + 1);
    }

    // ------- Timer -------
    namespace Timer {
        inline constexpr uint8_t BUFLEN{16};
        inline constexpr int64_t TH{3600000000000LL}, TM{60000000000LL}, TS{1000000000LL}, TMS{1000000LL};
    }

    // ------- Hardware -------
    namespace Hardware {
        enum Device : uint16_t { TEMPERATURE = 1 << 0, RGB = 1 << 1, BATTERY = 1 << 2, MOTORS = 1 << 3, GYRO = 1 << 4, USD = 1 << 5, MIC = 1 << 6, SPEAKER = 1 << 7, TFT = 1 << 8, SERIAL = 1 << 9, WIFI = 1 << 10, CLOCK = 1 << 11 };
        inline constexpr std::array<std::string_view, 12> DEVICES{ "TEMPERATURE", "RGB", "BATTERY", "MOTORS", "GYRO", "USD", "MIC", "SPEAKER", "TFT", "SERIAL", "WIFI", "CLOCK" };
        static_assert(DEVICES.size() == std::bit_width(std::to_underlying(Device::CLOCK)));
        inline constexpr std::array<gpio_num_t, 8> DIRS{GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_13, GPIO_NUM_14}; // IN1, IN2, IN1, IN2, IN1, IN2, IN1, IN2
        inline constexpr std::array<gpio_num_t, 4> ENS{GPIO_NUM_6, GPIO_NUM_9, GPIO_NUM_18, GPIO_NUM_0}; // ENA, ENC, ENB, END
        inline constexpr std::array<gpio_num_t, 3> SPKPINS{GPIO_NUM_12, GPIO_NUM_38, GPIO_NUM_39}; // BCLK, LRC, DIN
        inline constexpr std::array<gpio_num_t, 3> MICPINS{GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42}; // SCK, WS, SD
        inline constexpr std::array<gpio_num_t, 5> TFTPINS{GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_47, GPIO_NUM_21}; // SCK, MOSI, DC, CS, RST
        inline constexpr gpio_num_t TRIGPIN{GPIO_NUM_43}, ECHOPIN{GPIO_NUM_44}, SCLPIN{GPIO_NUM_10}, SDAPIN{GPIO_NUM_11}, BATPIN{GPIO_NUM_1}; // TRIG, ECHO, SCL, SDA, BAT
        inline constexpr uint16_t BIASN{200}, POSTMS{60}, MICMS{100}, BATSAMPLES{8}, BATNUM{49}, BATDEN{10}, WIFIMS{4000}, NTPMS{5000}, WIFISETTLEMS{250};
        inline constexpr uint32_t I2CHZ{400000}, SPIHZ{40000000}, PWMHZ{20000}, MICHZ{16000}, SPKHZ{44100};
        inline constexpr uint8_t PWMBITS{8}, GYROADDR{0x68}, GYRO_Z_REG{0x47};
        inline constexpr float GYROSCALE{65.5f};
        inline constexpr uint8_t GYRO_WHO_AM_I{0x75};
        inline constexpr std::array<uint8_t, 6> GYRO_INIT_CONFIG{0x6B, 0x00, 0x1A, 0x03, 0x1B};
        inline constexpr float US_SOUND_DIVISOR{58.0f};
        inline constexpr float US_MAX_DISTANCE{400.0f};
    }

    // ------- Network -------
    namespace Network {
        inline constexpr std::string_view SSID{"WIN-7CBRB18PEP0 5553"};
        inline constexpr std::string_view PASS{"e8&761M27"};
        inline constexpr std::string_view NTPHOST{"time.google.com"};
        inline constexpr std::string_view DDNS{"8.8.8.8"};
        inline constexpr std::string_view TZONE{"IST-5:30"};
        inline constexpr std::string_view URIROOT{"/"};
        inline constexpr std::string_view URILOGS{"/Logs/*"};
        inline constexpr std::string_view URITELEMETRY{"/Telemetry"};
    }

    // ------- Drive -------
    namespace Drive {
        inline constexpr uint8_t DEFAULTSPEED{128}, PRIORITY{10}, CORE{0};
        inline constexpr uint16_t STACK{4096};
        inline constexpr float HDGKP{1.5f}, HDGKI{0.02f}, HDGKD{0.8f}, INTEGMAX{2000.0f};
        inline constexpr float PDKP{2.0f}, PDKD{0.5f}, STANDOFF{20.0f}, HARDSTOP{5.0f}, TURNDEG{90.0f}, TURNTOL{2.0f};
        inline constexpr uint32_t TASK_DELAY_MS{10};
        inline constexpr uint8_t METRICS_INTERVAL_MOD{100};
    }

    // ------- Actions -------
    namespace Actions {
        enum Action : uint8_t { STOP, FORWARD, REVERSE, LEFT, RIGHT, STATUS };
        inline constexpr std::array<std::string_view, 6> NAMES{ "STOP", "FORWARD", "REVERSE", "LEFT", "RIGHT", "STATUS" };
        static_assert(NAMES.size() == STATUS + 1);
    }

    // ------- Audio -------
    namespace Audio {
        inline constexpr float THRESHOLD{0.85f};
        inline constexpr uint16_t FRAME{512}, WINFRAMES{3000 / 32}, BUFLEN{2048}, STACK{8192}; // 32ms frames at 16kHz
        inline constexpr uint8_t PRIORITY{7}, CORE{1};
        inline constexpr std::array<std::string_view, 14> COMMANDS{ "GO FORWARD", "GO BACKWARD", "TURN LEFT", "TURN RIGHT", "STOP MOVING", "REPORT STATUS", "CHECK BATTERY", "SAY HELLO", "TELL ME A JOKE", "WHAT TIME IS IT", "GOOD MORNING", "GOOD NIGHT", "THANK YOU", "SHOW YOUR FACE" };
        static_assert(FRAME <= BUFLEN);
    }

    // ------- UI (GC9A01 240x240 Round TFT) -------
    namespace UI {
        struct Job { uint16_t INTERVAL, DUR; Actions::Action ACT; };
        inline constexpr uint16_t WIDTH{240}, HEIGHT{240}, CENTERX{120}, CENTERY{120}, REFRESHRATE{50}, BATMIN{9000}, BATSPAN{36}, STACK{4096};
        enum Expression : uint8_t { HAPPY, WORRIED, SLEEPY, DEAD };
        inline constexpr std::array<std::string_view, 4> NAMES{ "HAPPY", "WORRIED", "SLEEPY", "DEAD"};
        inline constexpr uint8_t STRIPROWS{8}, PRIORITY{4}, CORE{1}, FACEFIRST{8}, FACELAST{22}, EYEDX{50}, EYEW{30}, EYEH{40}, GAZE{10};
        inline constexpr uint8_t LIDMAX{4}, BLINKFRAMES{6}, BLINKMIN{40}, BLINKMAX{120}, SLEEPYPCT{15}, RINGIN{110}, RINGOUT{114}, RINGSTEP{2};
        inline constexpr uint16_t EYECOLOR{0x96FF}; // RGB565
        inline constexpr std::array<uint16_t, 4> RINGCOLORS{0x2D08, 0xBD45, 0xCB63, 0xB945}; // green, yellow, orange, red (muted)
        inline constexpr std::array<uint8_t, 3> RINGLIMITS{60, 35, 15};
        inline constexpr uint8_t CASET{0x2A}, RASET{0x2B}, RAMWR{0x2C}, RAMWRC{0x3C};
        inline constexpr std::array<uint8_t, 4> FULL{0, 0, 0, WIDTH - 1};
        inline constexpr uint32_t TFTDELA{10}, TFTDELB{120}, TFTIDEL{150};
        inline constexpr size_t STATUSBUF{80};
        inline constexpr std::string_view URIROOT{"/"};
        inline constexpr std::string_view URILOGS{"/Logs/*"};
        inline constexpr std::string_view URITELEMETRY{"/Telemetry"};
        inline constexpr std::array<uint8_t, 224> TFTINIT{
            0xEF,0, 0xEB,1,0x14, 0xFE,0, 0xEF,0, 0xEB,1,0x14,
            0x84,1,0x40, 0x85,1,0xFF, 0x86,1,0xFF, 0x87,1,0xFF, 0x88,1,0x0A, 0x89,1,0x21, 0x8A,1,0x00, 0x8B,1,0x80, 0x8C,1,0x01, 0x8D,1,0x01, 0x8E,1,0xFF, 0x8F,1,0xFF,
            0xB6,2,0x00,0x00, 0x36,1,0x48, 0x3A,1,0x05, 0x90,4,0x08,0x08,0x08,0x08, // MADCTL = MX|BGR, COLMOD = 16-bit
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
}
