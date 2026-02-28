// ========== LIBRARIES ==========
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Bounce2.h>
#include "esp_timer.h"
#include <WiFi.h>
#include <WebServer.h>

// ========== WIFI CREDENTIALS ==========
  const char* ssid     = "YOUR_SSID";
  const char* password = "YOUR_PASSWORD";

WebServer server(6767);

// ========== CONSTANTS ==========

#define OFF_BUTTON        16
#define SELECTOR_BUTTON   17
#define OK_BUTTON          5

#define OFF_LED           15
#define SELECTOR_LED       2
#define OK_LED             4
#define ON_INDICATOR_LED  19

#define IR_SENSOR         18

#define RELAY             23

#define FLOWSENSOR        25  // Available GPIO on ESP32

#define MAX_HOURS_TIMER 6

enum modeStates {OFF, ON, ERROR_STATE};

// Declaration for SSD1306 display connected using I2C
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========== VARIABLES ==========
Bounce buttonOff      = Bounce();
Bounce buttonSelector = Bounce();
Bounce buttonOk       = Bounce();

int hourIndicator{0};
modeStates modeIndicator{OFF};

bool    isTimerRunning = false;
int64_t timerStartTime = 0;
int64_t timerDuration  = 0;

// ========== FLOW SENSOR VARIABLES ==========
volatile uint32_t flowPulseCount = 0;   // Incremented by ISR
float    flowRateLPH    = 0.0f;         // Litres per hour, updated every second
uint32_t lastFlowCalcMs = 0;            // millis() timestamp of last calculation

// ========== FLOW SENSOR ISR ==========
void IRAM_ATTR flowISR() {
  flowPulseCount++;
}

// ========== HTML PAGE (stored in program memory) ==========
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Control Bomba</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Rajdhani:wght@400;600;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg:        #0a0e14;
      --panel:     #111820;
      --border:    #1e3a4a;
      --accent:    #00d4ff;
      --accent2:   #ff6b35;
      --green:     #39ff14;
      --danger:    #ff2244;
      --text:      #cde8f0;
      --muted:     #4a7a8a;
      --glow:      0 0 12px rgba(0,212,255,0.4);
      --glow-green:0 0 12px rgba(57,255,20,0.5);
    }

    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      background: var(--bg);
      color: var(--text);
      font-family: 'Rajdhani', sans-serif;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 24px 16px 40px;
      background-image:
        radial-gradient(ellipse 60% 40% at 50% 0%, rgba(0,212,255,0.06) 0%, transparent 70%),
        repeating-linear-gradient(0deg, transparent, transparent 39px, rgba(0,212,255,0.03) 39px, rgba(0,212,255,0.03) 40px),
        repeating-linear-gradient(90deg, transparent, transparent 39px, rgba(0,212,255,0.03) 39px, rgba(0,212,255,0.03) 40px);
    }

    /* ---- HEADER ---- */
    header {
      display: flex;
      align-items: center;
      gap: 14px;
      margin-bottom: 28px;
      width: 100%;
      max-width: 420px;
    }
    .logo-dot {
      width: 10px; height: 10px;
      border-radius: 50%;
      background: var(--accent);
      box-shadow: var(--glow);
      animation: pulse 2s ease-in-out infinite;
    }
    h1 {
      font-family: 'Share Tech Mono', monospace;
      font-size: 1.1rem;
      letter-spacing: 0.25em;
      color: var(--accent);
      text-transform: uppercase;
    }
    .status-pill {
      margin-left: auto;
      font-size: 0.7rem;
      font-family: 'Share Tech Mono', monospace;
      letter-spacing: 0.15em;
      padding: 4px 10px;
      border-radius: 20px;
      border: 1px solid var(--muted);
      color: var(--muted);
      transition: all 0.3s;
    }
    .status-pill.on  { border-color: var(--green); color: var(--green); box-shadow: var(--glow-green); }
    .status-pill.off { border-color: var(--muted); color: var(--muted); }

    /* ---- CARDS ---- */
    .card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 20px 24px;
      width: 100%;
      max-width: 420px;
      margin-bottom: 16px;
      position: relative;
      overflow: hidden;
    }
    .card::before {
      content: '';
      position: absolute;
      top: 0; left: 0; right: 0;
      height: 2px;
      background: linear-gradient(90deg, transparent, var(--accent), transparent);
      opacity: 0.5;
    }
    .card-label {
      font-size: 0.7rem;
      letter-spacing: 0.2em;
      text-transform: uppercase;
      color: var(--muted);
      margin-bottom: 8px;
      font-family: 'Share Tech Mono', monospace;
    }
    .card-value {
      font-family: 'Share Tech Mono', monospace;
      font-size: 2.4rem;
      color: var(--accent);
      text-shadow: var(--glow);
      line-height: 1;
    }
    .card-unit {
      font-size: 0.85rem;
      color: var(--muted);
      margin-top: 4px;
      font-family: 'Share Tech Mono', monospace;
    }
    .card-row {
      display: flex;
      gap: 12px;
    }
    .card-row .card {
      flex: 1;
      margin-bottom: 0;
    }

    /* ---- HOUR SELECTOR ---- */
    .section-label {
      font-size: 0.7rem;
      letter-spacing: 0.2em;
      text-transform: uppercase;
      color: var(--muted);
      margin-bottom: 10px;
      font-family: 'Share Tech Mono', monospace;
      width: 100%;
      max-width: 420px;
    }
    .hour-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
      width: 100%;
      max-width: 420px;
      margin-bottom: 16px;
    }
    .btn-hour {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 10px;
      color: var(--text);
      font-family: 'Share Tech Mono', monospace;
      font-size: 1.1rem;
      padding: 16px 8px;
      cursor: pointer;
      transition: all 0.15s;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 2px;
    }
    .btn-hour span.sub { font-size: 0.6rem; color: var(--muted); letter-spacing: 0.1em; }
    .btn-hour:hover  { border-color: var(--accent); color: var(--accent); box-shadow: var(--glow); }
    .btn-hour:active { transform: scale(0.95); }
    .btn-hour.selected { border-color: var(--green); color: var(--green); box-shadow: var(--glow-green); }

    /* ---- OFF BUTTON ---- */
    .btn-off {
      width: 100%;
      max-width: 420px;
      padding: 16px;
      border-radius: 10px;
      background: transparent;
      border: 1px solid var(--danger);
      color: var(--danger);
      font-family: 'Share Tech Mono', monospace;
      font-size: 0.9rem;
      letter-spacing: 0.2em;
      cursor: pointer;
      transition: all 0.15s;
    }
    .btn-off:hover  { background: rgba(255,34,68,0.1); box-shadow: 0 0 12px rgba(255,34,68,0.3); }
    .btn-off:active { transform: scale(0.98); }

    /* ---- ANIMATIONS ---- */
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50%       { opacity: 0.3; }
    }

    /* ---- TOAST ---- */
    #toast {
      position: fixed;
      bottom: 24px;
      left: 50%;
      transform: translateX(-50%) translateY(80px);
      background: var(--panel);
      border: 1px solid var(--accent);
      color: var(--accent);
      font-family: 'Share Tech Mono', monospace;
      font-size: 0.8rem;
      letter-spacing: 0.1em;
      padding: 10px 20px;
      border-radius: 8px;
      box-shadow: var(--glow);
      transition: transform 0.3s ease;
      z-index: 100;
    }
    #toast.show { transform: translateX(-50%) translateY(0); }
  </style>
</head>
<body>

  <header>
    <div class="logo-dot"></div>
    <h1>Control Bomba</h1>
    <div class="status-pill off" id="statusPill">OFFLINE</div>
  </header>

  <!-- Stats row -->
  <div class="card-row" style="max-width:420px;width:100%;margin-bottom:16px;">
    <div class="card" style="margin-bottom:0">
      <div class="card-label">Tiempo restante</div>
      <div class="card-value" id="timeRemaining">--:--</div>
      <div class="card-unit">HH:MM</div>
    </div>
    <div class="card" style="margin-bottom:0">
      <div class="card-label">Flujo actual</div>
      <div class="card-value" id="flowRate">0.0</div>
      <div class="card-unit">L / hora</div>
    </div>
  </div>

  <!-- Hour selector -->
  <div class="section-label">// Seleccionar duración</div>
  <div class="hour-grid" id="hourGrid">
    <button class="btn-hour" onclick="setHours(1)"><span>1</span><span class="sub">HORA</span></button>
    <button class="btn-hour" onclick="setHours(2)"><span>2</span><span class="sub">HORAS</span></button>
    <button class="btn-hour" onclick="setHours(3)"><span>3</span><span class="sub">HORAS</span></button>
    <button class="btn-hour" onclick="setHours(4)"><span>4</span><span class="sub">HORAS</span></button>
    <button class="btn-hour" onclick="setHours(5)"><span>5</span><span class="sub">HORAS</span></button>
    <button class="btn-hour" onclick="setHours(6)"><span>6</span><span class="sub">HORAS</span></button>
  </div>

  <button class="btn-off" onclick="turnOff()">&#9632; APAGAR BOMBA</button>

  <div id="toast"></div>

  <script>
    let selectedHours = null;

    function pad(n) { return String(n).padStart(2, '0'); }

    function showToast(msg) {
      const t = document.getElementById('toast');
      t.textContent = msg;
      t.classList.add('show');
      setTimeout(() => t.classList.remove('show'), 2500);
    }

    function setHours(h) {
      selectedHours = h;
      // Highlight selected button
      document.querySelectorAll('.btn-hour').forEach((b, i) => {
        b.classList.toggle('selected', i + 1 === h);
      });
      fetch('/set?hours=' + h)
        .then(r => r.text())
        .then(() => showToast('Bomba activada por ' + h + (h === 1 ? ' hora' : ' horas')))
        .catch(() => showToast('Error de conexion'));
    }

    function turnOff() {
      fetch('/off')
        .then(r => r.text())
        .then(() => {
          showToast('Bomba apagada');
          selectedHours = null;
          document.querySelectorAll('.btn-hour').forEach(b => b.classList.remove('selected'));
        })
        .catch(() => showToast('Error de conexion'));
    }

    function pollStatus() {
      fetch('/status')
        .then(r => r.json())
        .then(data => {
          // Remaining time
          const totalSec = Math.max(0, Math.floor(data.remainingSeconds));
          const hh = Math.floor(totalSec / 3600);
          const mm = Math.floor((totalSec % 3600) / 60);
          document.getElementById('timeRemaining').textContent =
            data.isRunning ? pad(hh) + ':' + pad(mm) : '--:--';

          // Flow
          document.getElementById('flowRate').textContent =
            parseFloat(data.flowLPH).toFixed(1);

          // Status pill
          const pill = document.getElementById('statusPill');
          if (data.mode === 1) { // ON
            pill.textContent = 'ACTIVO';
            pill.className = 'status-pill on';
          } else {
            pill.textContent = 'EN ESPERA';
            pill.className = 'status-pill off';
            // Clear button highlight if stopped externally
            if (selectedHours !== null && !data.isRunning) {
              selectedHours = null;
              document.querySelectorAll('.btn-hour').forEach(b => b.classList.remove('selected'));
            }
          }
        })
        .catch(() => {
          document.getElementById('statusPill').textContent = 'OFFLINE';
          document.getElementById('statusPill').className = 'status-pill off';
        });
    }

    // Poll every second
    setInterval(pollStatus, 1000);
    pollStatus();
  </script>
</body>
</html>
)rawliteral";

// ========== FLOW RATE CALCULATION ==========
// Called every loop; updates flowRateLPH once per second.
void updateFlowRate() {
  uint32_t now = millis();
  if (now - lastFlowCalcMs >= 1000) {
    // Atomically snapshot and reset the counter
    noInterrupts();
    uint32_t pulses = flowPulseCount;
    flowPulseCount  = 0;
    interrupts();

    // YF-S201 formula: Hz = 7.5 * Q(L/min)  →  L/hour = pulses * 60 / 7.5
    flowRateLPH     = (float)pulses * 60.0f / 7.5f;
    lastFlowCalcMs  = now;
  }
}

// ========== RELAY HANDLER ==========
void handleRelay(){
  if (modeIndicator == ON && !isTimerRunning) {
    timerDuration  = (int64_t)(hourIndicator + 1) * 3600LL * 1000000LL;
    isTimerRunning = true;
    timerStartTime = esp_timer_get_time();
    digitalWrite(RELAY, HIGH);
    Serial.println("Relay ON. Timer: " + String(hourIndicator + 1) + " h");
  }

  if (isTimerRunning) {
    int64_t currentMicros = esp_timer_get_time();
    if ((currentMicros - timerStartTime) >= timerDuration) {
      digitalWrite(RELAY, LOW);
      modeIndicator  = OFF;
      isTimerRunning = false;
      Serial.println("Timer finalizado. Relay OFF.");
    }
  }
}

// ========== STATE / LED HANDLER ==========
void handleStates(){
  static int64_t lastMicrosLed = 0;
  int64_t currentMicros = esp_timer_get_time();

  if (modeIndicator == OFF) {
    digitalWrite(OFF_LED,      LOW);
    digitalWrite(ON_INDICATOR_LED, HIGH);
    digitalWrite(SELECTOR_LED, HIGH);
    if (currentMicros - lastMicrosLed >= 1000000) {
      digitalWrite(OK_LED, !digitalRead(OK_LED));
      lastMicrosLed = currentMicros;
    }
  } else if (modeIndicator == ON) {
    digitalWrite(SELECTOR_LED, LOW);
    digitalWrite(OK_LED,       LOW);
    if (currentMicros - lastMicrosLed >= 1000000) {
      digitalWrite(OFF_LED, !digitalRead(OFF_LED));
      digitalWrite(ON_INDICATOR_LED, !digitalRead(ON_INDICATOR_LED));
      lastMicrosLed = currentMicros;
    }
  }
}

// ========== PHYSICAL BUTTON HANDLER ==========
void readButtons(){
  buttonOff.update();
  buttonSelector.update();
  buttonOk.update();

  if (buttonOff.fell() && modeIndicator == ON) {
    digitalWrite(RELAY, LOW);
    isTimerRunning = false;
    modeIndicator  = OFF;
    Serial.println("Manual override. Relay OFF.");
  }
  if (buttonSelector.fell() && modeIndicator == OFF) {
    hourIndicator = (hourIndicator + 1) % 6;
  }
  if (buttonOk.fell() && modeIndicator == OFF) {
    modeIndicator = ON;
  }
}

// ========== DISPLAY ==========
void renderNumber(int number){
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(42, 29);
  display.println(number);
  display.setTextSize(1);
  display.setCursor(62, 42);
  display.println("HRS");
}

String formatNumber(int number){
  return (number < 10 ? "0" : "") + String(number);
}

void renderTime(int hour, int minute){
  String t = formatNumber(hour) + ":" + formatNumber(minute);
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(20, 29);
  display.println(t.c_str());
}

void renderTitle(String title){
  const int size = title.length() * 6;
  const int x    = 64 - (size / 2);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(x, 7);
  display.println(title.c_str());
}

void handleDisplay(){
  static int64_t lastMicrosDisplay = 0;
  static bool    displayActive     = false;
  static bool    displayCleared    = true;
  int64_t currentMicros = esp_timer_get_time();
  int presence = digitalRead(IR_SENSOR);

  if (presence == LOW) {
    displayActive     = true;
    lastMicrosDisplay = currentMicros;
    displayCleared    = false;
  }

  if (displayActive) {
    if ((currentMicros - lastMicrosDisplay) < 30LL * 1000000LL) {
      display.clearDisplay();
      if (modeIndicator == OFF) {
        renderTitle("ELIGE TEMPORIZADOR...");
        renderNumber(hourIndicator + 1);
      } else if (modeIndicator == ON && isTimerRunning) {
        renderTitle("TIEMPO RESTANTE");
        int64_t elapsed          = currentMicros - timerStartTime;
        int64_t remainingMicros  = timerDuration - elapsed;
        if (remainingMicros < 0) remainingMicros = 0;
        int totalSec = remainingMicros / 1000000;
        renderTime(totalSec / 3600, (totalSec % 3600) / 60);
      }
      display.display();
    } else {
      displayActive = false;
      if (!displayCleared) {
        display.clearDisplay();
        display.display();
        displayCleared = true;
      }
    }
  }
}

// ========== ERROR HANDLER ==========
void handleError(String errorMessage){
  modeIndicator = ERROR_STATE;
  digitalWrite(ON_INDICATOR_LED, LOW);
  while (true) {
    Serial.println("CRITICAL ERROR: " + errorMessage);
    digitalWrite(OFF_LED, HIGH); digitalWrite(SELECTOR_LED, HIGH); digitalWrite(OK_LED, HIGH);
    delay(200);
    digitalWrite(OFF_LED, LOW);  digitalWrite(SELECTOR_LED, LOW);  digitalWrite(OK_LED, LOW);
    delay(200);
    digitalWrite(OFF_LED, HIGH); digitalWrite(SELECTOR_LED, HIGH); digitalWrite(OK_LED, HIGH);
    delay(200);
    digitalWrite(OFF_LED, LOW);  digitalWrite(SELECTOR_LED, LOW);  digitalWrite(OK_LED, LOW);
    delay(600);
  }
}

// ========== SETUP ==========
void setup(){
  Serial.begin(9600);

  // Physical buttons
  pinMode(OFF_BUTTON,      INPUT_PULLUP);
  pinMode(SELECTOR_BUTTON, INPUT_PULLUP);
  pinMode(OK_BUTTON,       INPUT_PULLUP);

  buttonOff.attach(OFF_BUTTON);
  buttonSelector.attach(SELECTOR_BUTTON);
  buttonOk.attach(OK_BUTTON);
  buttonOff.interval(30);
  buttonSelector.interval(30);
  buttonOk.interval(30);

  // LEDs & relay
  pinMode(OFF_LED,          OUTPUT);
  pinMode(SELECTOR_LED,     OUTPUT);
  pinMode(OK_LED,           OUTPUT);
  pinMode(ON_INDICATOR_LED, OUTPUT);
  digitalWrite(ON_INDICATOR_LED, HIGH);
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);

  // IR sensor
  pinMode(IR_SENSOR, INPUT);

  // Flow sensor — interrupt on rising edge
  pinMode(FLOWSENSOR, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOWSENSOR), flowISR, RISING);

  // OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    handleError("Fallo display. Reinicia el equipo.");
  }
  delay(2000);
  display.clearDisplay();
  display.display();

  // WiFi
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());

  // Web server routes
  server.on("/", [](){
    server.send_P(200, "text/html", INDEX_HTML);
  });

  // /set?hours=N  — activate relay for N hours (1-6)
  server.on("/set", [](){
    if (server.hasArg("hours")) {
      int h = server.arg("hours").toInt();
      h = constrain(h, 1, 6);
      hourIndicator  = h - 1;          // match 0-based internal index
      modeIndicator  = ON;
      isTimerRunning = false;          // let handleRelay() arm the timer fresh
    }
    server.send(200, "text/plain", "OK");
  });

  // /off  — manual stop
  server.on("/off", [](){
    digitalWrite(RELAY, LOW);
    isTimerRunning = false;
    modeIndicator  = OFF;
    server.send(200, "text/plain", "OK");
  });

  // /status  — JSON for the web page to poll
  server.on("/status", [](){
    int64_t remainingSec = 0;
    if (isTimerRunning) {
      int64_t elapsed    = esp_timer_get_time() - timerStartTime;
      int64_t remaining  = timerDuration - elapsed;
      remainingSec       = (remaining > 0) ? remaining / 1000000LL : 0;
    }
    String json = "{";
    json += "\"mode\":"           + String((int)modeIndicator) + ",";
    json += "\"isRunning\":"      + String(isTimerRunning ? "true" : "false") + ",";
    json += "\"remainingSeconds\":"+ String((long)remainingSec) + ",";
    json += "\"flowLPH\":"        + String(flowRateLPH, 1);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado.");
}

// ========== LOOP ==========
void loop() {
  server.handleClient();    // Handle web requests
  readButtons();
  handleStates();
  handleRelay();
  handleDisplay();
  updateFlowRate();
}