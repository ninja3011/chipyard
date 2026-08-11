// ESP32 bridge implementing OpenOCD's "remote_bitbang" wire protocol, so the
// ESP32 can act as a JTAG adapter for TinyRocketConfig on Basys3 (PMOD JA)
// when no dedicated JTAG probe (FT232H/HS2/etc.) is available.
//
// Protocol verified directly against OpenOCD's own driver source
// (src/jtag/drivers/remote_bitbang.c, openocd-org/openocd, fetched 2026-08-10):
//   Client->server, one byte per command, no response unless noted:
//     '0'-'7' = write {tck,tms,tdi} = bits 2,1,0 of (c-'0')   -> set GPIOs
//     'R'                                                     -> respond '0' or '1' = sampled TDO
//     'r','s','t','u' = reset {trst,srst} = bits 1,0 of (c-'r') -> no-op (no physical TRST/SRST wired)
//     'B','b'  = blink on/off   -> no-op
//     'Z','z'  = sleep 1ms/1us  -> no-op (negligible next to WiFi latency)
//     'Q'      = quit           -> close this connection, wait for the next one
//     anything else (SWD chars 'd'-'f','O','o','c') -> ignored; we're JTAG-only
//
// Wiring (ESP32 -> Basys3 PMOD JA, see basys3.xdc for the JA pin locations):
//   GPIO18 -> JA0 (TCK)      GPIO19 -> JA1 (TMS)
//   GPIO21 -> JA2 (TDI)      GPIO22 <- JA3 (TDO)
//   GND    -> JA pin 5 or 6 (GND)
// These four GPIOs are plain, unreserved pins on a standard ESP32 DevKitC;
// double check your specific board doesn't use them for something else.

#include <WiFi.h>

// TODO: fill in your WiFi network before flashing.
static const char *WIFI_SSID = "YOUR_SSID_HERE";
static const char *WIFI_PASSWORD = "YOUR_PASSWORD_HERE";

static const uint16_t JTAG_TCP_PORT = 4444;

static const int PIN_TCK = 18;
static const int PIN_TMS = 19;
static const int PIN_TDI = 21;
static const int PIN_TDO = 22;

WiFiServer server(JTAG_TCP_PORT);

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_TCK, OUTPUT);
  pinMode(PIN_TMS, OUTPUT);
  pinMode(PIN_TDI, OUTPUT);
  pinMode(PIN_TDO, INPUT);
  digitalWrite(PIN_TCK, LOW);
  digitalWrite(PIN_TMS, LOW);
  digitalWrite(PIN_TDI, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. remote_bitbang host = ");
  Serial.print(WiFi.localIP());
  Serial.print("  port = ");
  Serial.println(JTAG_TCP_PORT);

  server.begin();
}

static inline void jtag_write(char c) {
  int v = c - '0';
  digitalWrite(PIN_TCK, (v & 0x4) ? HIGH : LOW);
  digitalWrite(PIN_TMS, (v & 0x2) ? HIGH : LOW);
  digitalWrite(PIN_TDI, (v & 0x1) ? HIGH : LOW);
}

void loop() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  Serial.println("OpenOCD connected");
  client.setNoDelay(true);

  while (client.connected()) {
    if (client.available() <= 0) {
      continue;
    }
    char c = client.read();

    if (c >= '0' && c <= '7') {
      jtag_write(c);
    } else if (c == 'R') {
      client.write(digitalRead(PIN_TDO) ? '1' : '0');
    } else if (c == 'Q') {
      break;
    }
    // 'r'-'u' (reset), 'B'/'b' (blink), 'Z'/'z' (sleep), and any SWD-only
    // chars are silently ignored -- no physical TRST/SRST line, and we
    // never negotiate the SWD transport.
  }

  client.stop();
  Serial.println("OpenOCD disconnected");
}
