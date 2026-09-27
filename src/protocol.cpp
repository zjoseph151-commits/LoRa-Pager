#include "protocol.h"

#include "config.h"

namespace {
bool parseSequence(const String& text, uint32_t& sequence) {
  if (text.isEmpty() || text.length() > 10) return false;
  uint64_t value = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text.charAt(i);
    if (c < '0' || c > '9') return false;
    value = value * 10 + c - '0';
    if (value > UINT32_MAX) return false;
  }
  sequence = static_cast<uint32_t>(value);
  return true;
}

bool parseHeader(const String& frame, const char* prefix, MessageFrame& out,
                 bool hasBody) {
  if (!frame.startsWith(prefix)) return false;
  int start = strlen(prefix);
  int end = frame.indexOf(',', start);
  if (end < 0) return false;
  out.sender = frame.substring(start, end);
  start = end + 1;
  end = frame.indexOf(',', start);
  if (end < 0) return false;
  out.target = frame.substring(start, end);
  start = end + 1;
  end = frame.indexOf(',', start);
  if (hasBody) {
    if (end < 0 || !parseSequence(frame.substring(start, end), out.sequence))
      return false;
    out.body = frame.substring(end + 1);  // commas in body are allowed
    return validBody(out.body);
  }
  out.body = "";
  return end < 0 && parseSequence(frame.substring(start), out.sequence);
}
}  // namespace

bool validBody(const String& body) {
  if (body.isEmpty() || body.length() > MAX_BODY_CHARS) return false;
  for (size_t i = 0; i < body.length(); ++i) {
    const char c = body.charAt(i);
    if (c < 32 || c > 126) return false;
  }
  return true;
}

bool parseMessageFrame(const String& frame, MessageFrame& out) {
  return parseHeader(frame, "SCBR,MSG,1,", out, true);
}

bool parseAckFrame(const String& frame, MessageFrame& out) {
  return parseHeader(frame, "SCBR,MACK,1,", out, false);
}

String makeMessageFrame(uint32_t sequence, const String& body) {
  return String("SCBR,MSG,1,") + DEVICE_ID + "," + PEER_ID + "," + sequence +
         "," + body;
}

String makeAckFrame(const String& target, uint32_t sequence) {
  return String("SCBR,MACK,1,") + DEVICE_ID + "," + target + "," + sequence;
}
