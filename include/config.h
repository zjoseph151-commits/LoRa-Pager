#pragma once

#include <Arduino.h>

// Wio-SX1262 for XIAO V1.0 through-hole/header board, not the rear B2B kit.
constexpr uint8_t PIN_LORA_NSS = 5;    // D4
constexpr uint8_t PIN_LORA_DIO1 = 2;   // D1
constexpr uint8_t PIN_LORA_RST = 3;    // D2
constexpr uint8_t PIN_LORA_BUSY = 4;   // D3
constexpr uint8_t PIN_LORA_RF_SW = 6;  // D5, HIGH receive / LOW transmit
constexpr uint8_t PIN_LORA_SCK = 7;    // D8
constexpr uint8_t PIN_LORA_MISO = 8;   // D9
constexpr uint8_t PIN_LORA_MOSI = 9;   // D10
constexpr uint8_t PIN_WIO_BUTTON = 1; // D0, optional board button

constexpr uint8_t PIN_I2C_SDA = 43;    // D6, Wio J1 pin 7 is NC
constexpr uint8_t PIN_I2C_SCL = 44;    // D7, Wio J2 pin 7 is NC

#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3C
#endif
#ifndef BUTTON_EXPANDER_ADDRESS
#define BUTTON_EXPANDER_ADDRESS 0x20
#endif
#ifndef ENABLE_ADS1115_POWER
#define ENABLE_ADS1115_POWER 0
#endif

constexpr char DEVICE_ID[] = "xiao-sx1262-ack";
constexpr char PEER_ID[] = "scoober-cardputer";
constexpr size_t MAX_BODY_CHARS = 64;
constexpr uint32_t SEND_COOLDOWN_MS = 10000;
constexpr uint32_t ACK_WINDOW_MS = 12000;

constexpr float RADIO_FREQ_MHZ = 915.0f;
constexpr float RADIO_BW_KHZ = 125.0f;
constexpr uint8_t RADIO_SF = 12;
constexpr uint8_t RADIO_CR = 5;
constexpr uint8_t RADIO_SYNC = 0x34;
constexpr uint16_t RADIO_PREAMBLE = 20;
constexpr int8_t RADIO_TX_DBM = 5;
