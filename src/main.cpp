#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <DHT.h>

#define PUMP_ENABLED true

#define SOIL_PIN        35   // P3  - soil sensor AO
#define SOIL_POWER_PIN  16   // P6  - powers the soil sensor only while reading
#define DHT_PIN         22   // DHT11 S
#define PUMP_PIN        21   // relay IN

int dryValue = 4095;    // raw soil reading in air (recalibrate in dry soil)
int wetValue = 1350;    // raw soil reading in water (recalibrate in wet soil)

int waterBelow = 40;    // water the plant when moisture is below this %
int doseTime = 3000;    // run the pump 3 seconds per dose

unsigned long soakTime = 15000;   // 15 s for testing (use 600000 = 10 min for real use)

// Display setup for the JC2432W328C board
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;
public:
  LGFX() {
    auto b = bus.config();
    b.spi_host = SPI2_HOST;
    b.freq_write = 40000000;
    b.pin_sclk = 14; b.pin_mosi = 13; b.pin_miso = 12; b.pin_dc = 2;
    bus.config(b);
    panel.setBus(&bus);

    auto p = panel.config();
    p.pin_cs = 15; p.pin_rst = -1;
    p.panel_width = 240; p.panel_height = 320;
    p.invert = false;
    panel.config(p);

    auto l = light.config();
    l.pin_bl = 27;
    light.config(l);
    panel.setLight(&light);

    setPanel(&panel);
  }
};

LGFX screen;
DHT dht(DHT_PIN, DHT11);

int moisture = 0;
int soilRaw = 0;
float temperature = 0;
float humidity = 0;
unsigned long lastWaterTime = 0;

// pump 
void pumpOn()  { digitalWrite(PUMP_PIN, LOW); }
void pumpOff() { digitalWrite(PUMP_PIN, HIGH); }

void showMessage(String msg, uint16_t color) {
  screen.fillRect(0, 250, 240, 30, TFT_BLACK);
  screen.setTextColor(color, TFT_BLACK);
  screen.setTextSize(2);
  screen.setCursor(10, 255);
  screen.print(msg);
}

// sensors
void readSensors() {
  digitalWrite(SOIL_POWER_PIN, HIGH);
  delay(100);
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(SOIL_PIN);
    delay(5);
  }
  digitalWrite(SOIL_POWER_PIN, LOW);

  soilRaw = total / 10;
  moisture = map(soilRaw, dryValue, wetValue, 0, 100);
  moisture = constrain(moisture, 0, 100);

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("DHT11 read failed - check wiring");
  } else {
    temperature = t;
    humidity = h;
  }
}

// Watering
void waterPlant() {
  int pumpTime = doseTime;

  // Hot or dry air = plant uses water faster so give bit more
  if (temperature > 27) pumpTime = pumpTime * 1.2;
  if (humidity < 35)    pumpTime = pumpTime * 1.2;

  Serial.println("PUMP ON");
  showMessage("Watering...", TFT_CYAN);
  pumpOn();
  delay(pumpTime);
  pumpOff();
  Serial.println("PUMP OFF");

  lastWaterTime = millis();
}

// screen
void drawScreen() {
  screen.setTextColor(TFT_WHITE, TFT_BLACK);

  // Soil moisture
  screen.setTextSize(2);
  screen.setCursor(10, 60);
  screen.print("Soil moisture:");
  screen.setTextSize(5);
  screen.setCursor(10, 85);
  screen.printf("%d%%  ", moisture);

  // Moisture bar with a yellow line at the target
  screen.drawRect(10, 140, 220, 20, TFT_WHITE);
  screen.fillRect(12, 142, 216, 16, TFT_BLACK);
  uint16_t barColor = (moisture < waterBelow) ? TFT_RED : TFT_GREEN;
  screen.fillRect(12, 142, 216 * moisture / 100, 16, barColor);
  screen.drawFastVLine(12 + 216 * waterBelow / 100, 138, 24, TFT_YELLOW);

  screen.setTextSize(1);
  screen.setTextColor(TFT_YELLOW, TFT_BLACK);
  screen.setCursor(10, 166);
  screen.printf("Target: above %d%%", waterBelow);

  // temperature and humidity
  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  screen.setTextSize(2);
  screen.setCursor(10, 190);
  screen.printf("Temp: %.1f C  ", temperature);
  screen.setCursor(10, 220);
  screen.printf("Humidity: %.0f%%  ", humidity);

  // Status
  if (moisture < waterBelow) showMessage("Soil is dry", TFT_ORANGE);
  else showMessage("Soil is OK ", TFT_GREEN);

  screen.setTextSize(1);
  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  screen.setCursor(10, 300);
  screen.printf("Raw soil: %d    ", soilRaw);
}

// Setup
void setup() {
  Serial.begin(115200);

  pinMode(PUMP_PIN, OUTPUT);
  pumpOff();
  pinMode(SOIL_POWER_PIN, OUTPUT);
  digitalWrite(SOIL_POWER_PIN, LOW);
  analogReadResolution(12);

  dht.begin();

  screen.init();
  screen.setBrightness(200);
  screen.fillScreen(TFT_BLACK);

  // title bar
  screen.fillRect(0, 0, 240, 40, TFT_DARKGREEN);
  screen.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  screen.setTextSize(2);
  screen.setCursor(45, 12);
  screen.print("Plant Monitor");

  Serial.println("time_s,soil_raw,moisture,temp_c,humidity");
}

// main loop
void loop() {
  readSensors();
  drawScreen();

  Serial.printf("%lu,%d,%d,%.1f,%.1f\n", millis() / 1000, soilRaw, moisture, temperature, humidity);

  // Water automatically if the soil is dry and enough time has passed
  bool waitedLongEnough = (millis() - lastWaterTime > soakTime);
  if (PUMP_ENABLED && moisture < waterBelow && waitedLongEnough) {
    waterPlant();
  }

  delay(2000);
}