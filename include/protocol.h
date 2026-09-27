#pragma once

#include <Arduino.h>

struct MessageFrame {
  String sender;
  String target;
  uint32_t sequence = 0;
  String body;
};

bool validBody(const String& body);
bool parseMessageFrame(const String& frame, MessageFrame& out);
bool parseAckFrame(const String& frame, MessageFrame& out);
String makeMessageFrame(uint32_t sequence, const String& body);
String makeAckFrame(const String& target, uint32_t sequence);
