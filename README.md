# XIAO ESP32-S3 LoRa handheld messenger

Standalone PlatformIO firmware for a **standard Seeed XIAO ESP32-S3** in the
**Wio-SX1262 for XIAO V1.0 header board**. The separate ESP32-S3 rear B2B kit
has a different pin map and is not supported by this environment.

The default PlatformIO environment is `xiao_esp32s3_header`. Open **this
repository** in VS Code with the PlatformIO extension. The Cardputer repository
was used only to check protocol and radio settings; nothing from it is needed
to build this project.

## Stage 1: Pin map and wiring

The [Seeed Wio header-board schematic](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262%20for%20XIAO%20V1.0_SCH.pdf)
shows the two seven-position headers. The [XIAO ESP32-S3 pin map](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
maps XIAO D numbers to ESP32 GPIO numbers.

| XIAO pad | GPIO | Wio header/net | Use in this project |
| --- | ---: | --- | --- |
| D0 | 1 | J1-1: Wio pushbutton to GND, 10 kΩ pull-up | Wio board button; left unused by the two-button UI |
| D1 | 2 | J1-2: radio DIO1 | Radio interrupt |
| D2 | 3 | J1-3: radio reset | Radio reset |
| D3 | 4 | J1-4: radio BUSY | Radio BUSY |
| D4 | 5 | J1-5: radio NSS | SPI chip select; **cannot be I²C SDA** |
| D5 | 6 | J1-6: radio RF_SW1 | External RF switch, HIGH receive / LOW transmit; **cannot be I²C SCL** |
| D6 | 43 | J1-7: not connected on Wio | **I²C SDA** |
| D7 | 44 | J2-7: not connected on Wio | **I²C SCL** |
| D8 | 7 | J2-6: radio SCK | SPI clock |
| D9 | 8 | J2-5: radio MISO | SPI MISO |
| D10 | 9 | J2-4: radio MOSI | SPI MOSI |
| 3V3 | — | J2-3 | 3.3 V peripherals |
| GND | — | J2-2 | Common ground |
| 5V | — | J2-1: USB VBUS | Optional USB-power sense only; no 5 V when running from LiPo |

**Important reference correction:** the Cardputer test node used GPIO1 as its
RF switch and GPIO21 as a button. The Wio V1.0 schematic instead puts RF_SW1
on D5/GPIO6 and the board pushbutton on D0/GPIO1; GPIO21 is the XIAO user LED.
The header radio NSS/DIO1/reset/BUSY/SPI map and modulation settings match the
tested node. [An independent header-board bring-up](https://github.com/meshtastic/firmware/issues/8409)
also uses GPIO6 for RXEN. Verify your board is marked **Wio-SX1262 for XIAO
V1.0**, then confirm D5 continuity to RF_SW1 if the revision is unclear.

The free D6/D7 signals reach the **unconnected seventh holes** of the two Wio
headers. They are electrically available when the boards are plugged together,
but ordinary non-stackable headers may not leave a convenient wire connection.
Use stackable headers, a breakout, or pre-solder insulated leads to those two
Wio holes before assembling the stack. Check continuity to XIAO D6/D7 and
check for shorts to adjacent SPI/power pads. Do not assume the XIAO underside
battery pads will be accessible after stacking.

### Shared I²C bus

The [ELEGOO module you selected](https://www.amazon.com/ELEGOO-Display-Compact-Self-Luminous-Projects/dp/B0FSRQG23K)
is a 128×64 SSD1306 I²C OLED, 7-bit address `0x3C`. Wire by the **printed pin
labels on the physical module**; connector order can vary.

| Part terminal | Connect to | Detail |
| --- | --- | --- |
| OLED GND | XIAO/Wio GND | Common ground |
| OLED VCC | 3V3 | Run the display and I²C pull-ups at 3.3 V |
| OLED SDA | D6/GPIO43, Wio J1-7 | Alternate SDA |
| OLED SCL | D7/GPIO44, Wio J2-7 | Alternate SCL |
| PCF8574 VCC/GND | 3V3/GND | **PCF8574**, not PCF8574A; address `0x20` |
| PCF8574 SDA/SCL | Same D6/D7 bus | Set A0, A1, A2 to GND; use 3.3 V I²C pull-ups (typically on a module, or add 4.7 kΩ each) |
| UP button | PCF8574 P0 → GND when pressed | Tap = up; hold 0.8 s = select |
| DOWN button | PCF8574 P1 → GND when pressed | Tap = down; hold 0.8 s = back/cancel |
| Former SELECT/BACK inputs | PCF8574 P2/P3 | Ignored by firmware; remove these switches only with USB and LiPo power disconnected |

Only D6 and D7 are free header GPIOs, so directly wired external buttons are
impractical alongside the OLED. The PCF8574 places the two active controls
on the existing I²C bus. No button resistors are needed on P0 or P1 for
the PCF8574's weak high-state pull-ups. The Wio's built-in D0 button is
independent of this UI.
D6/D7 are also the XIAO's UART0 TX/RX pads; this build keeps serial diagnostics
over native USB CDC instead of using that UART.

### Battery and optional measurement

| Connection | Wiring | What it tells firmware |
| --- | --- | --- |
| LiPo 3.7 V nominal, protected single cell | Solder `BAT+` to XIAO battery positive and `BAT−` to negative; Seeed says negative is the pad closest to USB-C | Powers board and charges from USB; **no built-in firmware battery reading** |
| USB-C | XIAO USB-C data/power | Native USB serial and onboard charging; firmware cannot infer USB solely from being awake |
| Optional ADS1115 VDD/GND/SDA/SCL | 3V3/GND/D6/D7, address `0x48` | Additional I²C voltage converter |
| Optional BAT+ sense divider | XIAO battery-positive pad → **100 kΩ** → ADS1115 A0; A0 → **33 kΩ** → GND; 100 nF from A0 to GND | Battery **terminal voltage**, even when USB is connected; does not prove a battery is attached |
| Optional USB VBUS divider | XIAO 5V/VBUS pad → **100 kΩ** → ADS1115 A1; A1 → **33 kΩ** → GND; 100 nF from A1 to GND | USB input voltage / presence separately from battery voltage |

The default `xiao_esp32s3_header` environment needs none of this optional
hardware. After fitting the two dividers and ADS1115, select the
`xiao_esp32s3_header_power` environment in PlatformIO. The display/serial then
show measured battery-terminal voltage and USB power.
There is deliberately **no battery percentage**: voltage alone is not a
reliable state-of-charge reading under load or charging. Seeed explicitly says
the [standard XIAO ESP32-S3 cannot read its battery voltage internally](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/#battery-usage),
and its red charge LED is the built-in charging indicator. A real software
charging indication requires an additional measurement of current into the
battery (for example, a correctly wired bidirectional I²C shunt sensor placed
**in series** with BAT+) or access to a documented charger status output. The
present firmware reports charging as **unknown**, even with ADS1115, and never
infers it from battery voltage or USB presence. The on-board red LED can be
checked visually during hardware bring-up.

Use an antenna suitable for 915 MHz on the Wio before any transmit test.

## Stage 2: Controls and messaging

| State | UP tap (P0) | UP hold 0.8 s | DOWN tap (P1) | DOWN hold 0.8 s |
| --- | --- | --- | --- | --- |
| Browse | Older message | Open a new blank draft | Newer message | Jump to newest message |
| Compose | Previous wheel choice | Pick shown character or `SEND`/`DELETE` | Next wheel choice | Cancel draft and return to Browse |

Tap actions occur on **release**, so holding a button does not also move the
selection. Auto-repeat is disabled; each tap moves one step. The former P2/P3
button inputs are ignored. The character wheel contains
`SEND`, `DELETE`, space, A–Z, a–z, digits, and common punctuation. Compose
starts at `A`; one UP selects space, another UP selects `DELETE`, and a third
UP selects `SEND`. Hold UP on a letter to append it, on `DELETE` to erase
the last character, or on `SEND` to transmit the draft. The draft limit is
64 printable ASCII characters.
The display shows recent inbound/outbound messages, `WAIT`, `ACK`, and `NO ACK`.
History and duplicate tracking are held in RAM and reset on power cycle.
No periodic beacon is transmitted. The radio stays awake to receive messages.

Wire format:

```text
SCBR,MSG,1,<sender>,<target>,<decimal uint32 sequence>,<1–64 printable ASCII body>
SCBR,MACK,1,<sender>,<target>,<same sequence>
```

This device is `xiao-sx1262-ack`; its only accepted peer is
`scoober-cardputer`. The body may contain commas. A received message gets a
matching ACK, including duplicates; duplicate messages are shown only once.
An outgoing message is marked delivered **only** after a matching sender,
target, and sequence ACK arrives. Otherwise it shows `NO ACK` after 12 seconds.
Manual message sends have a 10-second cooldown. The sequence starts from a
random 32-bit value each boot, as in the tested node. There is no encryption or
multi-hop routing in this protocol.

Radio settings copied from the working node: **915.0 MHz, 125 kHz, SF12,
CR 4/5, sync word `0x34`, preamble 20, 5 dBm TX, 3.0 V TCXO, DC-DC mode**.
The firmware also sets a 60 mA SX1262 current limit and DIO2 RF switching.

### USB serial diagnostics

Open the PlatformIO monitor at 115200 baud. Native USB CDC stays enabled while
D6/D7 are I²C. Commands, each followed by Enter:

| Command | Action |
| --- | --- |
| `s` | Print radio, UI, ACK, and power-sensing status |
| `i` | Scan the alternate I²C bus (`0x3C` OLED, `0x20` PCF8574, optional `0x48` ADS1115) |
| `p` | Read raw PCF8574 input levels and retry button detection if it was absent at boot |
| `o` | Force every OLED pixel on for three seconds using direct SSD1306 commands, then restore the message screen |
| `r` | Retry radio initialization after checking hardware |
| `m hello` | Send an ASCII message directly |
| `u`, `d`, `e`, `b` | Simulate UP tap, DOWN tap, UP hold/select, DOWN hold/back for bench UI checks |
| `send`, `cancel` | Send the current draft or cancel it via USB serial |
| `h` | Print command help |

The OLED and button expander can be absent during initial bench checks;
firmware continues with USB serial. Without the expander, use serial commands
for composition and sending. Incoming frames and radio errors are logged.

### Bring up the two active buttons

The selected [Comimark PCF8574T breakout](https://www.amazon.com/dp/B07X3KWQZ7)
answers at `0x20`. The firmware writes `0xFF` so its port pins act as inputs;
the [PCF8574 datasheet](https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf)
describes the weak high current source. P2 and P3 are ignored. Only P0 and P1
drive the UI.

1. Run `i`: expect `0x20` for the expander and `0x3C` (or `0x3D`) for the OLED.
   Run `s`: expect `buttons=ready`.
2. Run `p` with both active buttons released. P0 and P1 should read `up`.
   Hold one at a time: P0/UP or P1/DOWN should change to `PRESSED`; the monitor
   logs presses, releases, taps, and holds. P2/P3 readings are diagnostic only.
3. Hold UP for 0.8 s to enter Compose. Tap DOWN to move from `A` to `B`;
   hold UP to append `B`. Tap UP to return to `A`, then UP to space, then UP
   to `DELETE`; hold UP to erase `B`. Hold DOWN to cancel and return to Browse.
   This sequence does not transmit.
4. For a send test, compose a visible character, choose `SEND` on the wheel,
   and hold UP. Attach a 915 MHz antenna and bring the peer online first.

If `i` shows only the OLED, check the expander's power and SDA/SCL contacts.
If its address is not `0x20`, set `-DBUTTON_EXPANDER_ADDRESS=0xNN` in
`platformio.ini` to the scanned address and rebuild. PCF8574 uses `0x20`–
`0x27`; PCF8574A uses `0x38`–`0x3F`. Do not assign the expander the OLED's
address. If P0 or P1 stays `up` while held, fully remove USB and LiPo power,
then test its P-pin-to-GND path for near-zero resistance while pressed. Never
use resistance or continuity mode while either power source is connected.

### If the OLED has power but stays dark

Run `i` in the USB serial monitor. It reports the idle levels of GPIO43/44
and all responding I²C addresses. The OLED normally answers at `0x3C`; the
firmware also tries `0x3D` during initialization. Both idle line levels should
be `1` because SDA and SCL are pulled up to 3.3 V.

| Observation | Next check |
| --- | --- |
| `0x3C` or `0x3D` is absent, but `0x20` appears | The alternate I²C bus works. Recheck the OLED's SDA/SCL contacts, pin labels, and address or try another module. Power alone does not establish an I²C connection. |
| No device appears, or either line stays at `0` | Recheck the D6/D7 header taps and 3.3 V pull-ups; a stuck-low line can block the whole bus. |
| OLED address appears | Enter `o`. A solid panel for three seconds confirms that raw SSD1306 commands reach the panel. If it lights but the normal UI remains blank, report the serial output so the rendering path can be isolated. |
| OLED acknowledges but the `o` test stays dark | The display controller, its charge-pump circuit, or the module itself may differ from the listed SSD1306. Check the module marking and try another supplied OLED. |

If `SCL=GPIO44 level=0`, leave the radio wiring alone and isolate the I²C
clock path:

1. Power off. Disconnect the OLED and any button expander from **SCL**, then
   check continuity from XIAO **D7/GPIO44** through Wio **J2-7** to the OLED
   clock wire. Confirm that line has no short to GND or neighboring pads.
2. Power from USB and measure D7 to GND. It should idle near **3.3 V**. If it
   floats or stays low with all I²C devices disconnected, fit a **4.7 kΩ**
   pull-up from D7 to 3V3 and measure again. A line that rises now was missing
   an effective pull-up; a line that remains near 0 V needs a short/pin-tap
   investigation before reconnecting modules.
3. Reconnect the OLED alone and measure both D7 and its SCL terminal. If D7
   becomes low only when the OLED is connected, check the module pin labels,
   wire orientation, and try another OLED from the pack. Reconnect any expander
   only after the OLED answers the `i` scan at `0x3C` or `0x3D`.

The XIAO D7 pad is [GPIO44](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/);
the [Wio V1.0 schematic](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262%20for%20XIAO%20V1.0_SCH.pdf)
marks J2-7 unconnected to its radio circuit. Espressif recommends external
I²C pull-ups, typically **1–10 kΩ**; the internal pull-ups can be too weak for
a reliable bus. See the [ESP32-S3 I²C guidance](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-reference/peripherals/i2c.html).

## Stage 3: Hardware checks and upload

Confirm power and wiring before each hardware change. The OLED and PCF8574
have been detected on the user's physical stack; the new two-button mapping
still needs its own on-device check after uploading this build.

1. Confirm the Wio board marking is the **header-connected V1.0 board** and
   the XIAO is the **standard ESP32-S3**. Inspect the D6/D7 taps and continuity
   to J1-7/J2-7. Check for shorts between every adjacent header pad.
2. With the stack unpowered, verify LiPo polarity at the XIAO battery pads and
   insulate/strain-relieve the joints. Keep the 5V pin away from the LiPo.
3. Confirm OLED label-to-wire mapping, 3.3 V supply, PCF8574 address straps,
   and that I²C pull-ups go to 3.3 V. Check UP and DOWN connect P0 and P1
   respectively to GND when pressed. P2 and P3 are ignored.
4. Attach a 915 MHz antenna. Then connect USB and inspect the board's charge
   LED according to the [Seeed battery instructions](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/#battery-usage).
5. Once wiring is confirmed, build and upload from PlatformIO. Use `i` to see
   `0x3C` and `0x20`; use `s` to verify `radio=ready`. Test the two active
   controls with the no-transmit sequence above, then compose a short message
   using the `SEND` wheel action and verify that the Cardputer sees it and the
   display changes from `WAIT` to `ACK`. Repeat with the Cardputer offline to
   verify `NO ACK`, then send a duplicate inbound frame and verify one history
   entry and a second ACK.

The build command, if needed outside VS Code, is
`platformio run -e xiao_esp32s3_header`. It **only builds**. Upload is a
separate PlatformIO action after wiring confirmation.
