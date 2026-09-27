#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"
#include "protocol.h"

namespace {

SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST,
                          PIN_LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);

enum class UiMode : uint8_t { Browse, Compose };
enum class Delivery : uint8_t { Received, Waiting, Delivered, TimedOut };
enum class Button : uint8_t { Up, Down, Select, Back };

constexpr const char* BUTTON_NAMES[] = {"UP", "DOWN", "UNUSED", "ACTION"};

struct HistoryEntry {
  String body;
  uint32_t sequence = 0;
  bool outgoing = false;
  Delivery delivery = Delivery::Received;
};

struct ButtonState {
  bool raw = false;
  bool stable = false;
  bool longSent = false;
  uint32_t changedMs = 0;
  uint32_t pressedMs = 0;
  uint32_t repeatedMs = 0;
};

constexpr uint8_t HISTORY_SIZE = 6;
constexpr uint8_t RECENT_SIZE = 6;
constexpr uint32_t DEBOUNCE_MS = 35;
constexpr uint32_t LONG_PRESS_MS = 800;
constexpr char CHAR_PALETTE[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?'-:/";
constexpr size_t PALETTE_ACTIONS = 2;  // SEND and DELETE precede SPACE.
constexpr size_t PALETTE_SIZE = PALETTE_ACTIONS + sizeof(CHAR_PALETTE) - 1;
constexpr size_t PALETTE_START = PALETTE_ACTIONS + 1;  // A.

volatile bool packetReceived = false;
bool radioReady = false;
bool listening = false;
bool displayReady = false;
bool buttonsReady = false;
uint8_t displayAddress = OLED_I2C_ADDRESS;
bool oledTestActive = false;
uint32_t oledTestStartedMs = 0;
int lastRadioState = 0;
uint32_t rxCount = 0;
uint32_t txCount = 0;
uint32_t crcCount = 0;
uint32_t txFailCount = 0;
uint32_t ackCount = 0;
uint32_t lastSendMs = 0;
uint32_t sequenceCounter = 0;
uint32_t pendingSequence = 0;
uint32_t ackStartedMs = 0;
bool awaitingAck = false;
HistoryEntry history[HISTORY_SIZE];
uint8_t historyCount = 0;
uint8_t selectedHistory = 0;
uint32_t recentSequences[RECENT_SIZE] = {};
uint8_t recentCount = 0;
ButtonState buttonStates[4];
UiMode uiMode = UiMode::Browse;
String draft;
size_t paletteIndex = 0;
String statusText = "Starting";
String serialLine;
bool dirty = true;
uint32_t lastRenderMs = 0;

#if ENABLE_ADS1115_POWER
bool powerSensorReady = false;
float batteryTerminalV = 0;
float usbV = 0;
uint32_t lastPowerReadMs = 0;
constexpr uint8_t ADS1115_ADDRESS = 0x48;
constexpr float DIVIDER_RATIO = (100000.0f + 33000.0f) / 33000.0f;
#endif

void IRAM_ATTR onPacketReceived() { packetReceived = true; }

bool probeI2c(uint8_t address) {
  if (digitalRead(PIN_I2C_SDA) == LOW || digitalRead(PIN_I2C_SCL) == LOW)
    return false;
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void scanI2c() {
  const int sdaLevel = digitalRead(PIN_I2C_SDA);
  const int sclLevel = digitalRead(PIN_I2C_SCL);
  Serial.printf("I2C SDA=GPIO%u level=%d SCL=GPIO%u level=%d\n",
                PIN_I2C_SDA, sdaLevel, PIN_I2C_SCL, sclLevel);
  if (sdaLevel == LOW || sclLevel == LOW) {
    Serial.println(F("I2C scan skipped: bus held low. Check pull-ups, "
                     "D6/D7 continuity, and connected modules."));
    return;
  }
  Serial.print(F("I2C devices:"));
  bool any = false;
  for (uint8_t address = 0x08; address < 0x78; ++address) {
    if (probeI2c(address)) {
      Serial.printf(" 0x%02X", address);
      any = true;
    }
  }
  if (!any) Serial.print(F(" none"));
  Serial.println();
}

void setStatus(const String& text) {
  statusText = text;
  dirty = true;
  Serial.println(text);
}

void pushHistory(const String& body, uint32_t sequence, bool outgoing,
                 Delivery delivery) {
  for (int i = min(static_cast<int>(historyCount),
                   static_cast<int>(HISTORY_SIZE) - 1);
       i > 0; --i)
    history[i] = history[i - 1];
  history[0] = {body, sequence, outgoing, delivery};
  if (historyCount < HISTORY_SIZE) ++historyCount;
  selectedHistory = 0;
  dirty = true;
}

bool isDuplicate(uint32_t sequence) {
  for (uint8_t i = 0; i < recentCount; ++i)
    if (recentSequences[i] == sequence) return true;
  return false;
}

void rememberSequence(uint32_t sequence) {
  for (int i = min(static_cast<int>(recentCount),
                   static_cast<int>(RECENT_SIZE) - 1);
       i > 0; --i)
    recentSequences[i] = recentSequences[i - 1];
  recentSequences[0] = sequence;
  if (recentCount < RECENT_SIZE) ++recentCount;
}

void prepareReceive() { digitalWrite(PIN_LORA_RF_SW, HIGH); }
void prepareTransmit() { digitalWrite(PIN_LORA_RF_SW, LOW); }

bool startListening() {
  if (!radioReady) return false;
  prepareReceive();
  packetReceived = false;
  lastRadioState = radio.startReceive();
  listening = lastRadioState == RADIOLIB_ERR_NONE;
  if (!listening) setStatus(String("Listen error ") + lastRadioState);
  return listening;
}

bool transmitFrame(const String& frame) {
  if (!radioReady) {
    setStatus("Radio unavailable");
    return false;
  }
  listening = false;
  packetReceived = false;
  prepareTransmit();
  String payload = frame;  // RadioLib's String transmit overload is mutable.
  lastRadioState = radio.transmit(payload);
  prepareReceive();
  packetReceived = false;
  const bool sent = lastRadioState == RADIOLIB_ERR_NONE;
  if (sent) {
    ++txCount;
    Serial.printf("TX: %s\n", frame.c_str());
  } else {
    ++txFailCount;
    setStatus(String("TX error ") + lastRadioState);
  }
  startListening();
  return sent;
}

bool initRadio() {
  radioReady = false;
  listening = false;
  packetReceived = false;
  pinMode(PIN_LORA_RF_SW, OUTPUT);
  prepareReceive();
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  lastRadioState = radio.begin(RADIO_FREQ_MHZ, RADIO_BW_KHZ, RADIO_SF,
                               RADIO_CR, RADIO_SYNC, RADIO_TX_DBM,
                               RADIO_PREAMBLE, 3.0f, false);
  if (lastRadioState != RADIOLIB_ERR_NONE) {
    setStatus(String("Radio init error ") + lastRadioState);
    return false;
  }
  radioReady = true;
  radio.setCurrentLimit(60.0f);
  radio.setDio2AsRfSwitch(true);
  radio.setPacketReceivedAction(onPacketReceived);
  setStatus("Radio ready");
  return startListening();
}

void sendMessage(const String& body) {
  if (!validBody(body)) {
    setStatus("Use 1-64 ASCII chars");
    return;
  }
  if (awaitingAck) {
    setStatus("Waiting for ACK");
    return;
  }
  const uint32_t now = millis();
  if (lastSendMs && now - lastSendMs < SEND_COOLDOWN_MS) {
    setStatus(String("Wait ") +
              ((SEND_COOLDOWN_MS - (now - lastSendMs) + 999) / 1000) + "s");
    return;
  }
  const uint32_t nextSequence = ++sequenceCounter;
  if (!transmitFrame(makeMessageFrame(nextSequence, body))) return;
  lastSendMs = now;
  pendingSequence = nextSequence;
  ackStartedMs = millis();
  awaitingAck = true;
  pushHistory(body, nextSequence, true, Delivery::Waiting);
  uiMode = UiMode::Browse;
  draft = "";
  setStatus(String("Sent #") + nextSequence + " waiting ACK");
}

void handleFrame(const String& frame) {
  MessageFrame parsed;
  if (parseAckFrame(frame, parsed)) {
    if (parsed.sender == PEER_ID && parsed.target == DEVICE_ID &&
        awaitingAck && parsed.sequence == pendingSequence) {
      awaitingAck = false;
      ++ackCount;
      for (uint8_t i = 0; i < historyCount; ++i) {
        if (history[i].outgoing && history[i].sequence == pendingSequence) {
          history[i].delivery = Delivery::Delivered;
          break;
        }
      }
      setStatus(String("Delivered #") + pendingSequence);
    }
    return;
  }
  if (!parseMessageFrame(frame, parsed) || parsed.sender != PEER_ID ||
      parsed.target != DEVICE_ID)
    return;
  if (!isDuplicate(parsed.sequence)) {
    rememberSequence(parsed.sequence);
    ++rxCount;
    pushHistory(parsed.body, parsed.sequence, false, Delivery::Received);
    setStatus(String("Received #") + parsed.sequence);
    Serial.printf("Message: %s\n", parsed.body.c_str());
  } else {
    Serial.printf("Duplicate #%lu; ACK again\n",
                  static_cast<unsigned long>(parsed.sequence));
  }
  if (transmitFrame(makeAckFrame(parsed.sender, parsed.sequence))) ++ackCount;
}

void serviceRadio() {
  if (!packetReceived || !listening) return;
  packetReceived = false;
  listening = false;
  String frame;
  lastRadioState = radio.readData(frame);
  if (lastRadioState == RADIOLIB_ERR_NONE) {
    Serial.printf("RX RSSI=%.1f SNR=%.1f: %s\n", radio.getRSSI(),
                  radio.getSNR(), frame.c_str());
    handleFrame(frame);
  } else if (lastRadioState == RADIOLIB_ERR_CRC_MISMATCH) {
    ++crcCount;
    setStatus("RX CRC mismatch");
  } else {
    setStatus(String("RX error ") + lastRadioState);
  }
  if (!listening) startListening();
}

void serviceAckTimeout() {
  if (!awaitingAck || millis() - ackStartedMs < ACK_WINDOW_MS) return;
  awaitingAck = false;
  for (uint8_t i = 0; i < historyCount; ++i) {
    if (history[i].outgoing && history[i].sequence == pendingSequence) {
      history[i].delivery = Delivery::TimedOut;
      break;
    }
  }
  setStatus(String("No ACK #") + pendingSequence);
}

void printStatus() {
  Serial.printf(
      "radio=%s listening=%s state=%d rx=%lu tx=%lu txfail=%lu crc=%lu "
      "ack=%lu pending=%s oled=%s buttons=%s\n",
      radioReady ? "ready" : "down", listening ? "yes" : "no",
      lastRadioState, static_cast<unsigned long>(rxCount),
      static_cast<unsigned long>(txCount),
      static_cast<unsigned long>(txFailCount),
      static_cast<unsigned long>(crcCount),
      static_cast<unsigned long>(ackCount), awaitingAck ? "yes" : "no",
      displayReady ? "yes" : "no", buttonsReady ? "yes" : "no");
#if ENABLE_ADS1115_POWER
  if (powerSensorReady)
    Serial.printf("BAT terminal=%.2fV USB VBUS=%.2fV charging=unknown\n",
                  batteryTerminalV, usbV);
  else
    Serial.println(F("Power sensor absent; charging=unknown"));
#else
  Serial.println(F("Battery voltage=unavailable USB=unavailable charging=unknown"));
#endif
}

void shortButton(Button button) {
  if (uiMode == UiMode::Browse) {
    switch (button) {
      case Button::Up:
        if (selectedHistory + 1 < historyCount) ++selectedHistory;
        break;
      case Button::Down:
        if (selectedHistory > 0) --selectedHistory;
        break;
      case Button::Select:
        uiMode = UiMode::Compose;
        draft = "";
        paletteIndex = PALETTE_START;
        break;
      case Button::Back: break;
    }
  } else {
    switch (button) {
      case Button::Up:
        paletteIndex = (paletteIndex + PALETTE_SIZE - 1) % PALETTE_SIZE;
        break;
      case Button::Down:
        paletteIndex = (paletteIndex + 1) % PALETTE_SIZE;
        break;
      case Button::Select:
        if (paletteIndex == 0) sendMessage(draft);
        else if (paletteIndex == 1) {
          if (!draft.isEmpty()) draft.remove(draft.length() - 1);
        } else if (draft.length() < MAX_BODY_CHARS) {
          draft += CHAR_PALETTE[paletteIndex - PALETTE_ACTIONS];
        } else {
          setStatus("Draft full");
        }
        break;
      case Button::Back: break;
    }
  }
  dirty = true;
}

void longButton(Button button) {
  if (button == Button::Back) {
    if (uiMode == UiMode::Compose) {
      uiMode = UiMode::Browse;
      draft = "";
      setStatus("Draft cancelled");
    } else selectedHistory = 0;
  } else if (button == Button::Select && uiMode == UiMode::Compose) {
    sendMessage(draft);  // USB serial "send" command.
  }
  dirty = true;
}

bool readButtonLevels(uint8_t& levels) {
  if (digitalRead(PIN_I2C_SDA) == LOW || digitalRead(PIN_I2C_SCL) == LOW)
    return false;
  if (Wire.requestFrom(static_cast<uint8_t>(BUTTON_EXPANDER_ADDRESS),
                       static_cast<uint8_t>(1)) != 1)
    return false;
  levels = Wire.read();
  return true;
}

void serviceButtons() {
  static uint32_t lastPollMs = 0;
  const uint32_t now = millis();
  if (!buttonsReady || now - lastPollMs < 15) return;
  lastPollMs = now;
  uint8_t levels = 0;
  if (!readButtonLevels(levels)) {
    buttonsReady = false;
    setStatus("Button expander lost");
    return;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    if (i == 2) continue;  // Damaged SELECT wiring; P2 is unused.
    ButtonState& state = buttonStates[i];
    const bool pressed = (levels & (1u << i)) == 0;
    if (pressed != state.raw) {
      state.raw = pressed;
      state.changedMs = now;
    }
    if (state.raw != state.stable && now - state.changedMs >= DEBOUNCE_MS) {
      state.stable = state.raw;
      Serial.printf("Button %s %s (P%u)\n", BUTTON_NAMES[i],
                    state.stable ? "pressed" : "released", i);
      if (state.stable) {
        state.pressedMs = now;
        state.repeatedMs = now;
        state.longSent = false;
        if (i < 2) shortButton(static_cast<Button>(i));
      } else if (!state.longSent && i == 3) {
        Serial.println(F("ACTION tap -> select"));
        shortButton(Button::Select);
      }
    }
    if (state.stable && i == 3 && !state.longSent &&
        now - state.pressedMs >= LONG_PRESS_MS) {
      state.longSent = true;
      Serial.printf("Button %s long press\n", BUTTON_NAMES[i]);
      Serial.println(F("ACTION hold -> back"));
      longButton(Button::Back);
    }
    if (state.stable && i < 2 && now - state.pressedMs >= 500 &&
        now - state.repeatedMs >= 120) {
      state.repeatedMs = now;
      shortButton(static_cast<Button>(i));
    }
  }
}

void initButtons() {
  buttonsReady = probeI2c(BUTTON_EXPANDER_ADDRESS);
  if (!buttonsReady) {
    Serial.println(F("PCF8574 not found; serial controls remain available."));
    return;
  }
  Wire.beginTransmission(BUTTON_EXPANDER_ADDRESS);
  Wire.write(0xFF);  // PCF8574 quasi-bidirectional pins: HIGH means input.
  buttonsReady = Wire.endTransmission() == 0;
  Serial.printf("PCF8574 buttons %s at 0x%02X\n",
                buttonsReady ? "ready" : "failed", BUTTON_EXPANDER_ADDRESS);
}

void printButtons() {
  if (!buttonsReady) initButtons();
  if (!buttonsReady) {
    Serial.printf("No button expander at 0x%02X. Run i to scan I2C; "
                  "check A0/A1/A2 and VCC/SDA/SCL.\n",
                  BUTTON_EXPANDER_ADDRESS);
    return;
  }
  uint8_t levels = 0;
  if (!readButtonLevels(levels)) {
    buttonsReady = false;
    Serial.println(F("Button read failed; check I2C wiring."));
    return;
  }
  Serial.printf("PCF8574 0x%02X raw=0x%02X; "
                "P0/UP=%s P1/DOWN=%s P2/unused=%s P3/ACTION=%s\n",
                BUTTON_EXPANDER_ADDRESS, levels,
                (levels & 0x01) ? "up" : "PRESSED",
                (levels & 0x02) ? "up" : "PRESSED",
                (levels & 0x04) ? "up" : "PRESSED",
                (levels & 0x08) ? "up" : "PRESSED");
}

void initDisplay() {
  displayAddress = OLED_I2C_ADDRESS;
  displayReady = probeI2c(displayAddress);
  if (!displayReady) {
    const uint8_t alternate = displayAddress == 0x3C ? 0x3D : 0x3C;
    if (probeI2c(alternate)) {
      displayAddress = alternate;
      displayReady = true;
      Serial.printf("OLED found at alternate address 0x%02X.\n", alternate);
    }
  }
  if (!displayReady) {
    Serial.printf("SSD1306 not found at 0x%02X or 0x%02X.\n",
                  OLED_I2C_ADDRESS, OLED_I2C_ADDRESS == 0x3C ? 0x3D : 0x3C);
    return;
  }
  display.setI2CAddress(displayAddress << 1);  // U8g2 uses 8-bit address.
  display.setBusClock(100000);
  display.begin();
  display.setContrast(120);
  Serial.printf("SSD1306 128x64 initialized at 0x%02X.\n", displayAddress);
  dirty = true;
}

bool oledCommands(const uint8_t* commands, size_t count) {
  Wire.beginTransmission(displayAddress);
  Wire.write(0x00);  // Following bytes are SSD1306 commands.
  Wire.write(commands, count);
  const uint8_t result = Wire.endTransmission();
  if (result != 0)
    Serial.printf("OLED command I2C error %u at 0x%02X.\n", result,
                  displayAddress);
  return result == 0;
}

void startOledTest() {
  if (!displayReady) initDisplay();
  if (!displayReady) {
    Serial.println(F("OLED test skipped: no display address ACK."));
    return;
  }
  // Force the charge pump and display on, then ignore display RAM briefly.
  const uint8_t commands[] = {0x8D, 0x14, 0xAF, 0xA5};
  if (!oledCommands(commands, sizeof(commands))) return;
  oledTestActive = true;
  oledTestStartedMs = millis();
  Serial.println(F("OLED all-pixels-on test for 3 s; screen should turn solid."));
}

void serviceOledTest() {
  if (!oledTestActive || millis() - oledTestStartedMs < 3000) return;
  const uint8_t resumeRamDisplay = 0xA4;
  oledCommands(&resumeRamDisplay, 1);
  oledTestActive = false;
  dirty = true;
  Serial.println(F("OLED test ended; normal screen restored."));
}

void drawLine(uint8_t y, const String& text) {
  display.drawStr(0, y, text.substring(0, 25).c_str());
}

void render() {
  if (!displayReady || (!dirty && millis() - lastRenderMs < 2000)) return;
  lastRenderMs = millis();
  dirty = false;
  display.clearBuffer();
  display.setFont(u8g2_font_5x8_tr);
  if (uiMode == UiMode::Browse) {
    String header = String("LoRa ") + (radioReady ? "ON" : "ERR") + " ";
#if ENABLE_ADS1115_POWER
    if (powerSensorReady) {
      header += String(batteryTerminalV, 2) + "V ";
      header += usbV >= 4.25f ? "USB" : "U-";
    } else {
      header += awaitingAck ? "WAIT ACK" : statusText;
    }
#else
    header += awaitingAck ? "WAIT ACK" : statusText;
#endif
    drawLine(8, header);
    drawLine(17, String("History ") + (historyCount ? selectedHistory + 1 : 0) +
                     "/" + historyCount + " Tap=write");
    if (historyCount) {
      const HistoryEntry& entry = history[selectedHistory];
      const char* delivery = entry.delivery == Delivery::Received ? "RX" :
                             entry.delivery == Delivery::Waiting ? "WAIT" :
                             entry.delivery == Delivery::Delivered ? "ACK" : "NO ACK";
      drawLine(26, String(entry.outgoing ? "TX #" : "RX #") +
                       entry.sequence + " " + delivery);
      drawLine(35, entry.body.substring(0, 23));
      drawLine(44, entry.body.substring(23, 46));
      drawLine(53, entry.body.substring(46, 64));
    } else {
      drawLine(35, "No messages yet");
    }
    drawLine(63, "Up/Dn browse Hold=top");
  } else {
    drawLine(8, String("Compose ") + draft.length() + "/64");
    drawLine(17, statusText);
    const size_t start = draft.length() > 60 ? draft.length() - 60 : 0;
    drawLine(26, draft.substring(start, start + 20));
    drawLine(35, draft.substring(start + 20, start + 40));
    drawLine(44, draft.substring(start + 40, start + 60));
    String choice = paletteIndex == 0 ? "SEND" :
                    paletteIndex == 1 ? "DELETE" :
                    CHAR_PALETTE[paletteIndex - PALETTE_ACTIONS] == ' ' ?
                        "SPACE" :
                        String(CHAR_PALETTE[paletteIndex - PALETTE_ACTIONS]);
    drawLine(53, "Pick: [" + choice + "]");
    drawLine(63, "Tap=pick Hold=cancel");
  }
  display.sendBuffer();
}

#if ENABLE_ADS1115_POWER
bool readAdsChannel(uint8_t channel, float& volts) {
  // Single-shot, AINx/GND, PGA +/-4.096 V, 128 SPS, comparator disabled.
  const uint16_t config = 0x8000 | ((0x04 + channel) << 12) | 0x0200 |
                          0x0100 | 0x0080 | 0x0003;
  Wire.beginTransmission(ADS1115_ADDRESS);
  Wire.write(0x01);
  Wire.write(config >> 8);
  Wire.write(config & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  delay(9);
  Wire.beginTransmission(ADS1115_ADDRESS);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0 || Wire.requestFrom(ADS1115_ADDRESS, 2) != 2)
    return false;
  const int16_t raw = (Wire.read() << 8) | Wire.read();
  volts = raw * (4.096f / 32768.0f) * DIVIDER_RATIO;
  return true;
}

void servicePower() {
  if (!powerSensorReady || millis() - lastPowerReadMs < 5000) return;
  lastPowerReadMs = millis();
  if (!readAdsChannel(0, batteryTerminalV) || !readAdsChannel(1, usbV)) {
    powerSensorReady = false;
    Serial.println(F("ADS1115 read failed; power values unavailable."));
    return;
  }
  Serial.printf("Power BAT terminal=%.2fV USB=%s (%.2fV) charging=unknown\n",
                batteryTerminalV, usbV >= 4.25f ? "present" : "absent", usbV);
}
#endif

void serialCommand(const String& line) {
  if (line.startsWith("m ")) sendMessage(line.substring(2));
  else if (line == "s") printStatus();
  else if (line == "r") initRadio();
  else if (line == "i") scanI2c();
  else if (line == "p") printButtons();
  else if (line == "o") startOledTest();
  else if (line == "u") shortButton(Button::Up);
  else if (line == "d") shortButton(Button::Down);
  else if (line == "e") shortButton(Button::Select);
  else if (line == "b") longButton(Button::Back);
  else if (line == "send") longButton(Button::Select);
  else if (line == "cancel") longButton(Button::Back);
  else if (line == "h" || line == "help")
    Serial.println(F("m <text>, s=status, r=retry radio, i=I2C scan, "
                     "p=button levels/retry, "
                     "o=OLED all-on test, "
                     "u/d/e=move/pick, b=back, send, cancel"));
  else if (!line.isEmpty()) Serial.println(F("Unknown command; type h."));
}

void serviceSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (!serialLine.isEmpty()) serialCommand(serialLine);
      serialLine = "";
    } else if (c >= 32 && c <= 126 && serialLine.length() < 100) {
      serialLine += c;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println(F("XIAO SX1262 handheld messaging"));
  Serial.println(F("Header Wio board; attach a 915 MHz antenna before TX."));
  Serial.println(F("Controls: P0=UP P1=DOWN P3=ACTION "
                   "(tap=select hold=back); P2 unused."));
  Serial.printf("Radio pins NSS=%u DIO1=%u RST=%u BUSY=%u RF_SW=%u "
                "SPI=%u/%u/%u I2C SDA=%u SCL=%u\n",
                PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY,
                PIN_LORA_RF_SW, PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI,
                PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.println(F("Commands: m <text>, s, r, i, p, o, u, d, e, b, send, cancel"));
  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL))
    Serial.println(F("ERROR: I2C controller did not start."));
  Wire.setClock(100000);
  scanI2c();
  initDisplay();
  initButtons();
#if ENABLE_ADS1115_POWER
  powerSensorReady = probeI2c(ADS1115_ADDRESS);
  Serial.printf("ADS1115 %s\n", powerSensorReady ? "ready" : "absent");
#endif
  sequenceCounter = esp_random();
  initRadio();
  printStatus();
}

void loop() {
  serviceSerial();
  serviceButtons();
  serviceRadio();
  serviceAckTimeout();
  serviceOledTest();
#if ENABLE_ADS1115_POWER
  servicePower();
#endif
  render();
}
