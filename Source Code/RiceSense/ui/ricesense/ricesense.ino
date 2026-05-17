#include <lvgl.h>
#include <ui.h>
#include <LovyanGFX.hpp>
#include <EncoderRead.h>
#include "WiFi.h"
#include <Adafruit_TCS34725.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <SoftwareSerial.h>
#include "nvs_flash.h"
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "ApiSender.h"
#include <DFRobotDFPlayerMini.h>
#define MAX_GARDENS 5
#define MAX_RICECOLOR 100

Preferences prefs;
Preferences ricePrefs;
Preferences gardenPrefs;
static const int RXPin = 6, TXPin = 19;
static const uint32_t GPSBaud = 115200;
TinyGPSPlus gps;
SoftwareSerial gpsSerial(RXPin, TXPin);
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfplayer;

const char* server = "http://ricesense.cloud";
String MACHINE_ID = "8HI9L70C";
String wifiSSID = "";
String wifiPASS = "";
String savedSSID, savedPASS;

Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_120MS, TCS34725_GAIN_1X);
float currentR = 0.0;
float currentG = 0.0;
float currentB = 0.0;
float currentLat = 0.0;
float currentLng = 0.0;
int currentLCC = 0;
float percentage = 100;
bool isSyncing = false;

#define MAX_WIFI 10
String wifiList[MAX_WIFI];
int wifiCount = 0;
String selectedSSID = "";
#define ENC_A 18
#define ENC_B 15
#define ENC_SW 10
EncoderRead encoder(ENC_A, ENC_B, ENC_SW);

uint8_t gardenPointIndex = 1;
static unsigned long lastFixTime = 0;

struct PlotInfo {
  char plotName[32];   // แทน String
  uint16_t plotPoint;
  float lat;
  float lng;
};

struct Garden {
  char name[32];
  PlotInfo points[4];
};

struct RiceColorPoint {
  uint16_t point;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  float lat;
  float lng;
  uint32_t time;
  uint8_t lcc;
};
Garden allGardens[MAX_GARDENS];
int gardenCount = 0;

PlotInfo PLOT;
PlotInfo gardenPoints[4];   // เก็บ 4 จุด

RiceColorPoint RICECOLOR;
RiceColorPoint riceColors[MAX_RICECOLOR];
uint16_t riceColorCount = 0;
String GardenName = "";

lv_indev_t *encoder_indev = NULL;

lv_group_t *group_menu = NULL;
lv_group_t *group_color = NULL;
lv_group_t *group_smart = NULL;
lv_group_t *group_selectwifi = NULL;
lv_group_t *group_line = NULL;
lv_group_t *group_network = NULL;
lv_group_t *group_loginwifi = NULL;
lv_group_t *group_garden = NULL;
lv_group_t *group_gardenname = NULL;
lv_group_t *group_nogps = NULL;
lv_group_t *group_nogpsgarden = NULL;
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 55000000;
      cfg.freq_read = 25000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 13;
      cfg.pin_mosi = 11;
      cfg.pin_miso = 4;
      cfg.pin_dc = 21;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 5;
      cfg.pin_rst = 7;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[240 * 80];
void encoder_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  static int32_t last_enc = 0;
  static uint32_t boot_time = 0;

  if (boot_time == 0) boot_time = millis();

  int32_t counter = encoder.getCounter();
  bool btn_state = encoder.encBtn();  // pull-up

  if (millis() - boot_time < 500) {
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_REL;
    last_enc = counter;
    return;
  }

  data->enc_diff = counter - last_enc;
  data->state = btn_state ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

  last_enc = counter;
}
void capture_btn_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED) {
    RGBupdate();
  }
}
void RGBupdate() {
  uint16_t r, g, b, c, colorTemp, lux;

  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature(r, g, b);
  lux = tcs.calculateLux(r, g, b);
  const float rScale = 0.512;
  const float gScale = 1.140;
  const float bScale = 0.590;

  float red = min(r * rScale, 255.0f);
  float green = min(g * gScale, 255.0f);
  float blue = min(b * bScale, 255.0f);

  float chl = 26.6 + (0.449 * red) - (0.0588 * green) - (0.381 * blue);

  const float targetR[] = { 129, 103, 84, 74, 67, 62 };
  const float targetG[] = { 176, 144, 119, 104, 90, 82 };
  const float targetB[] = { 48, 40, 35, 31, 30, 28 };

  float L[6];
  float minDist = 99999;
  int minIndex = -1;
  float tillervalue = 0;
  float paniclevalue = 0;
  for (int i = 0; i < 6; i++) {
    L[i] = sqrt(
      pow((targetR[i] - red), 2) + pow((targetG[i] - green), 2) + pow((targetB[i] - blue), 2));

    if (L[i] < minDist) {
      minDist = L[i];
      minIndex = i;
    }
  }
  currentLCC = minIndex + 1;


  currentR = red;
  currentG = green;
  currentB = blue;  
  char buf[32];
  sprintf(buf,"%d",(int)red);
  lv_label_set_text(ui_RValue, buf);
  sprintf(buf,"%d",(int)green);
  lv_label_set_text(ui_GValue, buf);
  sprintf(buf,"%d",(int)blue);
  lv_label_set_text(ui_BValue, buf);
  sprintf(buf, "%d", currentLCC);
  lv_label_set_text(ui_GroupValue, buf);

  lv_color_t color = lv_color_make((uint8_t)red, (uint8_t)green, (uint8_t)blue);
  lv_obj_set_style_bg_color(ui_ColorBox, color, LV_PART_MAIN);
  if(currentLCC > 3){
    tillervalue = 5;
    paniclevalue =  9;
  }else if(currentLCC == 3){
    tillervalue = 8.5;
    paniclevalue =  12.5;
  }else{
    tillervalue = 12;
    paniclevalue =  16;
  }
  sprintf(buf, "%.1f", tillervalue);
  lv_label_set_text(ui_tillerText, buf);
  sprintf(buf, "%.1f", paniclevalue );
  lv_label_set_text(ui_panicleText, buf);
  dfplayer.play(4);
}
void color_save_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;

  if (!gps.location.isValid() || !gps.date.isValid() || !gps.time.isValid()) {
    lv_obj_clear_flag(ui_Capturecontain, LV_OBJ_FLAG_HIDDEN);
    dfplayer.play(2);
    lv_indev_set_group(encoder_indev, group_nogps);
    lv_group_focus_obj(ui_CaptureCloseBtn);

    return;
  }
  lv_obj_clear_flag(ui_Savecontain, LV_OBJ_FLAG_HIDDEN);


  RiceColorPoint p;

  p.point = getNextRicePoint();
  p.r = currentR;
  p.g = currentG;
  p.b = currentB;
  p.lat = gps.location.lat();
  p.lng = gps.location.lng();
  p.lcc = currentLCC;
  p.time = getEpochFromGPS(); 

  saveRiceColorToNVS(p);

}
void updateGPS() {
  if (gps.location.isValid()) {
    currentLat = gps.location.lat();
    currentLng = gps.location.lng();
  }
  char buf[24];
  sprintf(buf, "%.6f", currentLat);
  lv_label_set_text(ui_Latvalue, buf);
  sprintf(buf, "%.6f", currentLng);
  lv_label_set_text(ui_Lngvalue, buf);
}

void scanWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("WIFISCAN");
  wifiCount = 0;

  if (n == 0) {
      Serial.println("no networks found");
    return;
  }else{
    for (int i = 0; i < n && i < MAX_WIFI; i++) {
      wifiList[wifiCount] = WiFi.SSID(i);
      wifiCount++;
      Serial.print(WiFi.SSID(i));
      delay(10);
    }
    WiFi.scanDelete();
  }

}
void scanwifi_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_clear_flag(ui_WifiScancontain, LV_OBJ_FLAG_HIDDEN);
    Serial.println("แสน WIFI");
    
    lv_timer_create([](lv_timer_t *t){

      scanWiFi();
      updateWiFiButtons();
      dfplayer.play(5);
      lv_obj_add_flag(ui_WifiScancontain, LV_OBJ_FLAG_HIDDEN);

      lv_timer_del(t);

    }, 100, NULL);
  }
}
void updateWiFiButtons() {
  lv_obj_t *wifiBtns[10] = { ui_WiFiBtn1, ui_WiFiBtn2, ui_WiFiBtn3, ui_WiFiBtn4, ui_WiFiBtn5, ui_WiFiBtn6 , ui_WiFiBtn7 , ui_WiFiBtn8 , ui_WiFiBtn9 , ui_WiFiBtn10  };
  lv_obj_t *wifiTexts[10] = { ui_WifiName1, ui_WifiName2, ui_WifiName3, ui_WifiName4, ui_WifiName5, ui_WifiName6, ui_WifiName7, ui_WifiName8, ui_WifiName9, ui_WifiName10 };

  for (int i = 0; i < 10; i++) {
    if (i < wifiCount) {
      lv_label_set_text(wifiTexts[i], wifiList[i].c_str());
      lv_obj_clear_flag(wifiBtns[i], LV_OBJ_FLAG_HIDDEN);
      lv_group_add_obj(group_selectwifi, wifiBtns[i]);
    } else {
      lv_obj_add_flag(wifiBtns[i], LV_OBJ_FLAG_HIDDEN);
      lv_group_remove_obj(wifiBtns[i]);
    }
  }
  lv_group_add_obj(group_selectwifi, ui_Button1);
}

void save_garden_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;

  if (!gps.location.isValid()) return;

  strncpy(gardenPoints[gardenPointIndex - 1].plotName, "Garden A", sizeof(gardenPoints[0].plotName) - 1);
  gardenPoints[gardenPointIndex - 1].plotPoint = gardenPointIndex;
  gardenPoints[gardenPointIndex - 1].lat = gps.location.lat();
  gardenPoints[gardenPointIndex - 1].lng = gps.location.lng();

  gardenPointIndex++;

  if (gardenPointIndex <= 4) {
    updateGardenPointLabel();
    lv_indev_set_group(encoder_indev, group_gardenname);

  } else {
    // ครบ 4 จุด → ไปตั้งชื่อสวน
    lv_group_remove_obj(ui_SaveGarden_Btn);
    

    lv_group_add_obj(group_gardenname, ui_KeyboardGardenName);
    lv_group_focus_obj(ui_KeyboardGardenName);

    lv_obj_add_flag(ui_ContainerGarden, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_ContainerGardenname, LV_OBJ_FLAG_HIDDEN);
  }
}


void saveGardenToNVS(const Garden &g) {
  gardenPrefs.begin("riceDB", false);

  int count = gardenPrefs.getInt("garden_count", 0);
  bool updated = false;

  for (int i = 0; i < count; i++) {
    char key[20];
    sprintf(key, "garden_%d", i);

    Garden tmp;
    gardenPrefs.getBytes(key, &tmp, sizeof(tmp));

    if (strcmp(tmp.name, g.name) == 0) {
      gardenPrefs.putBytes(key, &g, sizeof(Garden));
      updated = true;
      break;
    }
  }

  if (!updated && count < MAX_GARDENS) {
    char key[20];
    sprintf(key, "garden_%d", count);
    gardenPrefs.putBytes(key, &g, sizeof(Garden));
    count++;
    gardenPrefs.putInt("garden_count", count);
  }

  gardenPrefs.end();
  showGardensInUI();
}
void showGardensInUI() {
  Garden gardens[MAX_GARDENS];
  int gardenCount = 0;
  loadGardensFromNVS(gardens, gardenCount);

  lv_obj_t* texts[5] = {
    ui_GardenName1, ui_GardenName2, ui_GardenName3, ui_GardenName4, ui_GardenName5
  };

  lv_obj_t* containers[5] = {
    ui_Containergar1, ui_Containergar2, ui_Containergar3, ui_Containergar4, ui_Containergar5
  };

  lv_obj_t* delBtns[5] = {
    ui_GardenDeleteBtn1, ui_GardenDeleteBtn2, ui_GardenDeleteBtn3, ui_GardenDeleteBtn4, ui_GardenDeleteBtn5
  };

  // ล้าง group ก่อน (กันซ้ำ)
  lv_group_remove_all_objs(group_garden);

  // ซ่อนทุก container ก่อน
  for (int i = 0; i < 5; i++) {
    lv_obj_add_flag(containers[i], LV_OBJ_FLAG_HIDDEN);
  }

  // แสดง + ใส่เข้า group ตามจำนวนสวน
  for (int i = 0; i < gardenCount && i < 5; i++) {
    lv_obj_clear_flag(containers[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(texts[i], gardens[i].name);

    // ใส่ container และปุ่มลบเข้า group
    lv_group_add_obj(group_garden, delBtns[i]);
  }
  lv_group_add_obj(group_garden, ui_AddGarden);
  lv_group_add_obj(group_garden, ui_Button5);
}

void saveRiceColorToNVS(const RiceColorPoint &p) {
  ricePrefs.begin("riceDB", false);

  uint16_t count = ricePrefs.getUShort("rc_count", 0);
  count++;

  String key = "rc_" + String(count);
  ricePrefs.putBytes(key.c_str(), &p, sizeof(RiceColorPoint));
  ricePrefs.putUShort("rc_count", count);

  ricePrefs.end();
  lv_timer_create([](lv_timer_t *t){
    lv_obj_add_flag(ui_Savecontain, LV_OBJ_FLAG_HIDDEN);
    dfplayer.play(1);
    lv_timer_del(t); 
  }, 500, NULL);
}
void saveWiFiToNVS(const String &ssid, const String &pass) {
  prefs.begin("wifiDB", false);

  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);

  prefs.end();
}
void updateGardenPointLabel() {
  char buf[8];
  sprintf(buf, "%d", gardenPointIndex);
  lv_label_set_text(ui_GardenPointNum, buf);
}
uint16_t getNextRicePoint() {
  ricePrefs.begin("riceDB", false);

  uint16_t nextPoint = ricePrefs.getUShort("next_point", 1); // เริ่มที่ 1
  ricePrefs.putUShort("next_point", nextPoint + 1);

  ricePrefs.end();
  return nextPoint;
}

void add_garden_btn_event_cb(lv_event_t * e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
      if (!gps.location.isValid() || !gps.date.isValid() || !gps.time.isValid()) {
    
        lv_obj_clear_flag(ui_Gardencontain, LV_OBJ_FLAG_HIDDEN);
        lv_indev_set_group(encoder_indev, group_nogpsgarden);
        
        dfplayer.play(2);

        Serial.println("GardenUI");
        lv_group_focus_obj(ui_CaptureCloseBtn1);
        


        return;
       }
  Garden gardens[MAX_GARDENS];
  int gardenCount = 0;
  loadGardensFromNVS(gardens, gardenCount);

  Serial.printf("ADD GARDEN | current = %d / %d\n", gardenCount, MAX_GARDENS);

  if (gardenCount >= MAX_GARDENS) {
    Serial.println("Garden full → cannot add more");
    return;
  }

  lv_scr_load(ui_CreateGarden);
  lv_indev_set_group(encoder_indev, group_gardenname);
  lv_group_focus_obj(ui_SaveGarden_Btn);

}

void loadGardensFromNVS(Garden *gardens, int &outCount) {
  gardenPrefs.begin("riceDB", true);

  int count = gardenPrefs.getInt("garden_count", 0);
  if (count > MAX_GARDENS) count = MAX_GARDENS;

  outCount = count;


  for (int i = 0; i < count; i++) {
    char key[20];
    sprintf(key, "garden_%d", i);

    Garden g;
    memset(&g, 0, sizeof(g));
    gardenPrefs.getBytes(key, &g, sizeof(Garden));

    gardens[i] = g;

    for (int p = 0; p < 4; p++) {
      Serial.printf("  Point %d | Lat:%.6f Lng:%.6f\n",
                    p + 1,
                    g.points[p].lat,
                    g.points[p].lng);
    }
  }

  gardenPrefs.end();
}
void loadRiceColorsFromNVS(RiceColorPoint *arr, uint16_t &outCount) {
  ricePrefs.begin("riceDB", true);

  outCount = ricePrefs.getUShort("rc_count", 0);

  for (uint16_t i = 1; i <= outCount; i++) {
    String key = "rc_" + String(i);

    if (ricePrefs.isKey(key.c_str())) {
      ricePrefs.getBytes(key.c_str(), &arr[i - 1], sizeof(RiceColorPoint));

      time_t t = arr[i - 1].time;
      struct tm *tm_info = localtime(&t);

      char buf[32];
      if (tm_info) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
      } else {
        strcpy(buf, "N/A");
      }

      Serial.printf(
        "Point:%d R:%d G:%d B:%d Lat:%.6f Lng:%.6f LCC:%d Time:%s\n",
        arr[i - 1].point,
        arr[i - 1].r,
        arr[i - 1].g,
        arr[i - 1].b,
        arr[i - 1].lat,
        arr[i - 1].lng,
        arr[i - 1].lcc,
        buf
      );
    }
  }

  ricePrefs.end();
}
bool loadWiFiFromNVS(String &ssid, String &pass) {
  prefs.begin("wifiDB", true);

  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");

  prefs.end();

  return ssid.length() > 0;
}

uint32_t getEpochFromGPS() {
  if (!gps.date.isValid() || !gps.time.isValid()) return 0;

  tm t;
  t.tm_year = gps.date.year() - 1900;
  t.tm_mon  = gps.date.month() - 1;
  t.tm_mday = gps.date.day();
  t.tm_hour = gps.time.hour();
  t.tm_min  = gps.time.minute();
  t.tm_sec  = gps.time.second();
  t.tm_isdst = 0;

  time_t epoch = mktime(&t) + 7 * 3600;
  return (uint32_t)epoch;
}
ApiSender api(server, MACHINE_ID.c_str());

void syncGardensFromNVS() {
  Garden gardens[MAX_GARDENS];
  int gardenCount = 0;

  loadGardensFromNVS(gardens, gardenCount);

  static const char* gardenNames[MAX_GARDENS];

  for (int i = 0; i < gardenCount; i++) {
    gardenNames[i] = gardens[i].name;   // char[32] → const char*
  }
  api.syncGardens(gardenNames, gardenCount);
}
void debugKeys() {
  ricePrefs.begin("riceDB", true);

  Serial.println("---- DEBUG KEYS ----");

  for (int i = 1; i <= 120; i++) {
    String key = "rc_" + String(i);

    if (ricePrefs.isKey(key.c_str())) {
      size_t len = ricePrefs.getBytesLength(key.c_str());
      Serial.printf("FOUND: %s (size=%d)\n", key.c_str(), len);
    }
  }

  ricePrefs.end();
}
void sendAndClearRiceData() {
  if (WiFi.status() != WL_CONNECTED) return;

  gardenPrefs.begin("riceDB", false);

  // ---------- SYNC GARDENS ----------
  syncGardensFromNVS();

  Garden gardens[MAX_GARDENS];
  int gardenCount = 0;
  loadGardensFromNVS(gardens, gardenCount);

  for (int g = 0; g < gardenCount; g++) {
    for (int p = 0; p < 4; p++) {
      PlotData plot;
      plot.plotName  = gardens[g].name;
      plot.plotPoint = p + 1;
      plot.lat       = gardens[g].points[p].lat;
      plot.lng       = gardens[g].points[p].lng;

      api.sendPlot(plot);
    }
  }
  gardenPrefs.end();

  // ---------- SCAN RICE COLOR ----------
  Serial.println(">>> Auto-sync RiceColor Starting...");
  ricePrefs.begin("riceDB", false);
  bool foundData = false;
  bool allSuccess = true;

  for (int i = 1; i <= MAX_RICECOLOR; i++) {
    String key = "rc_" + String(i);

    if (ricePrefs.isKey(key.c_str())) {
      foundData = true;

      RiceColorPoint p;
      ricePrefs.getBytes(key.c_str(), &p, sizeof(RiceColorPoint));

      RiceColorData color;
      color.measure_point = p.point;
      color.r = p.r;
      color.g = p.g;
      color.b = p.b;
      color.lat = p.lat;
      color.lng = p.lng;
      color.lcc = p.lcc;
      color.measured_at = p.time;

      Serial.printf("Sending rc_%d ...\n", i);

      if (api.sendRiceColor(color)) {
        ricePrefs.remove(key.c_str());
        Serial.printf("rc_%d: OK\n", i);
      } else {
        allSuccess = false;
        Serial.printf("rc_%d: FAILED\n", i);
      }

      yield(); // กัน watchdog
    }
  }

  // ---------- CHECK ----------
  if (!foundData) {
    Serial.println("No RiceColor data found.");
    ricePrefs.end();
    return;
  }

  if (allSuccess) {
    ricePrefs.putUShort("rc_count", 0);   // reset เฉย ๆ (ไม่ใช้ก็ได้)
    ricePrefs.putUShort("next_point", 1);
    Serial.println(">>> All RiceColor Sync Completed.");
  } else {
    Serial.println(">>> Some data failed. Will retry later.");
  }

  ricePrefs.end();
}
void wifi_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_target(e);

    for (int i = 0; i < wifiCount && i < 10; i++) {
      lv_obj_t *ref =
        i == 0 ? ui_WiFiBtn1 : i == 1 ? ui_WiFiBtn2
                          : i == 2 ? ui_WiFiBtn3
                          : i == 3 ? ui_WiFiBtn4
                          : i == 4 ? ui_WiFiBtn5
                          : i == 5 ? ui_WiFiBtn6
                          : i == 6 ? ui_WiFiBtn7
                          : i == 7 ? ui_WiFiBtn8
                          : i == 8 ? ui_WiFiBtn9
                                   : ui_WiFiBtn10;

      if (btn == ref) {
        selectedSSID = wifiList[i];

        lv_label_set_text(ui_WifiName, selectedSSID.c_str());
        lv_textarea_set_text(ui_WIFIpassword, "");

        lv_scr_load(ui_LoginWifi);
        lv_indev_set_group(encoder_indev, group_loginwifi);
        lv_group_focus_obj(ui_Keyboard1); 
      }
    }
  }
}
void keyboard_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_READY) { 
    String password = lv_textarea_get_text(ui_WIFIpassword);

    if (selectedSSID == "" || password.length() < 1) return;
    lv_obj_clear_flag(ui_Wificontain, LV_OBJ_FLAG_HIDDEN);
    
    WiFi.disconnect();
    delay(100);
    WiFi.begin(selectedSSID.c_str(), password.c_str());

    lv_timer_create([](lv_timer_t *t){
      static int timeout = 0;

      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        lv_obj_add_flag(ui_Wificontain, LV_OBJ_FLAG_HIDDEN);
        lv_scr_load(ui_SelectWifi);
        dfplayer.play(6);
        saveWiFiToNVS(selectedSSID, lv_textarea_get_text(ui_WIFIpassword));

        timeout = 0;
        lv_timer_del(t); 
      } 
      else if (timeout > 20) {
        Serial.println("Connect timeout");
        lv_obj_add_flag(ui_Wificontain, LV_OBJ_FLAG_HIDDEN);
        lv_scr_load(ui_SelectWifi);
        dfplayer.play(9);
        timeout = 0;
        lv_timer_del(t);
      }

      timeout++;

    }, 500, NULL);
  }

  if (code == LV_EVENT_CANCEL) {
    lv_group_focus_obj(ui_Button6);
  }
}
void captureclose_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;


}
void gardenclose_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
  lv_indev_set_group(encoder_indev, group_garden);
  lv_group_focus_obj(ui_AddGarden);
}
void KeyboardGardenName_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) { 
        const char *txt = lv_textarea_get_text(ui_GardenNameTextArea);

        // เช็คว่ากรอกชื่อหรือยัง
        if (txt == NULL || strlen(txt) < 1) {
            // ถ้ายังไม่กรอก ให้ focus ที่คีย์บอร์ดต่อ (หรือจะสั่งกระพริบแจ้งเตือนก็ได้)
            lv_group_focus_obj(ui_KeyboardGardenName);
            // lv_timer_create(hide_garden_fail_cb, 1000, NULL); 
            return; 
        }

        // --- เริ่มกระบวนการบันทึก ---
        Garden g;
        memset(&g, 0, sizeof(g));
        strncpy(g.name, txt, sizeof(g.name) - 1);

        // ใส่ 4 จุดที่เก็บไว้ใน gardenPoints[] ลงในโครงสร้าง Garden
        for (int i = 0; i < 4; i++) {
            g.points[i] = gardenPoints[i];
        }

        // บันทึกลง NVS
        saveGardenToNVS(g);
        
        Serial.print("Saved Garden: ");
        Serial.println(g.name);

        // ล้างค่าใน Textarea
        lv_textarea_set_text(ui_GardenNameTextArea, "");

        // จัดการ UI: ซ่อนหน้ากรอกชื่อ กลับไปหน้าหลัก
        lv_obj_add_flag(ui_ContainerGardenname, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_ContainerGarden, LV_OBJ_FLAG_HIDDEN);

        // รีเซ็ตสถานะสำหรับการเก็บจุดครั้งต่อไป
        gardenPointIndex = 1;
        updateGardenPointLabel();

        // คืนค่าระบบควบคุม (Encoder) ไปที่เมนูหลัก
        lv_indev_set_group(encoder_indev, group_menu);
        lv_group_add_obj(group_gardenname,ui_SaveGarden_Btn);
        lv_group_remove_obj(ui_KeyboardGardenName);
        lv_scr_load(ui_Menu);
        lv_group_focus_obj(ui_Colorbutton); // Focus จุดเริ่มต้นของเมนู
    }

    // if (code == LV_EVENT_CANCEL) {
    //     // ล้างค่าที่อาจจะพิมพ์ค้างไว้
    //     lv_textarea_set_text(ui_GardenNameTextArea, "");
        
    //     // กลับไปหน้าเมนูหลักทันทีโดยไม่บันทึก
    //     lv_indev_set_group(encoder_indev, group_menu);
    //     lv_scr_load(ui_Menu);
    // }
}
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p){
  // Serial.println("FLUSH START");

  if (color_p == NULL) {
    lv_disp_flush_ready(disp);
    // Serial.println("FLUSH NULL");
    return;
  }

  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  // Serial.println("SPI START");

  tft.setAddrWindow(area->x1, area->y1, w, h);

  // Serial.println("PUSH PIXELS");
  tft.pushPixels((uint16_t *)color_p, w * h, true);

  // Serial.println("SPI END");
  tft.endWrite();

  lv_disp_flush_ready(disp);
  // Serial.println("FLUSH DONE");
}
void uiBattery() {

    const int bufferSize = 10;
    static int buffer[bufferSize];
    static int index = 0;
    static bool full = false;

    // เก็บค่า ADC
    buffer[index] = analogRead(14);
    index++;

    if (index >= bufferSize) {
        index = 0;
        full = true;
    }

    if (!full) return;

    // ค่าเฉลี่ย
    long sum = 0;
    for (int i = 0; i < bufferSize; i++) {
        sum += buffer[i];
    }
    float avgADC = sum / (float)bufferSize;

    // แปลงเป็นแรงดัน
    float voltage = avgADC * (3.3 / 4095.0);
    float battVoltage = voltage * 2.0;

    // จำกัดช่วง
    battVoltage = constrain(battVoltage, 2.5, 4.2);

    const float voltTable[] = {2.5, 3.0, 3.2, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 4.0, 4.2};
    const int socTable[]   = {  0,  10,  20,  30,  40,  50,  60,  70,  80,  90, 100};

    float percentage = 0;

    // หา segment
    for (int i = 0; i < 10; i++) {
        if (battVoltage >= voltTable[i] && battVoltage <= voltTable[i+1]) {

            // interpolate
            float v1 = voltTable[i];
            float v2 = voltTable[i+1];
            int p1 = socTable[i];
            int p2 = socTable[i+1];

            percentage = p1 + (battVoltage - v1) * (p2 - p1) / (v2 - v1);
            break;
        }
    }

    percentage = constrain(percentage, 0, 100);

    char buf[16];
    sprintf(buf, "%.0f%%", percentage);
    lv_label_set_text(ui_Betterypercenttext, buf);
}
void updateSyncStatusUI() {
    ricePrefs.begin("riceDB", true);
    uint16_t count = ricePrefs.getUShort("rc_count", 0);
    ricePrefs.end();

    if (count > 0) {
        lv_label_set_text(ui_MenuStatusText, "ไม่ล่าสุด");
        isSyncing = false;
        lv_obj_set_style_text_color(ui_MenuStatusText, lv_color_hex(0xE6FF00), LV_PART_MAIN);
        
        lv_obj_set_style_bg_color(ui_StatusColor, lv_color_hex(0xE6FF00), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui_StatusColor, 255, LV_PART_MAIN);
    } 
    else {
        isSyncing = true;
        lv_label_set_text(ui_MenuStatusText, "ล่าสุด");
        
        lv_obj_set_style_text_color(ui_MenuStatusText, lv_color_hex(0x31FF00), LV_PART_MAIN);
        
        lv_obj_set_style_bg_color(ui_StatusColor, lv_color_hex(0x31FF00), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui_StatusColor, 255, LV_PART_MAIN);
    }
}

void garden_del_btn_event_cb(lv_event_t * e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;

  int index = (int)lv_event_get_user_data(e);


  deleteGardenFromNVS(index);

  showGardensInUI();
}
void deleteGardenFromNVS(int index) {
  gardenPrefs.begin("riceDB", false);

  int count = gardenPrefs.getInt("garden_count", 0);
  if (index < 0 || index >= count) {
    gardenPrefs.end();
    return;
  }

  // เลื่อนสวนตัวหลังขึ้นมาแทน
  for (int i = index; i < count - 1; i++) {
    char keyFrom[20], keyTo[20];
    sprintf(keyFrom, "garden_%d", i + 1);
    sprintf(keyTo,   "garden_%d", i);

    Garden g;
    gardenPrefs.getBytes(keyFrom, &g, sizeof(Garden));
    gardenPrefs.putBytes(keyTo, &g, sizeof(Garden));
  }

  // ลบตัวสุดท้าย
  char lastKey[20];
  sprintf(lastKey, "garden_%d", count - 1);
  gardenPrefs.remove(lastKey);

  gardenPrefs.putInt("garden_count", count - 1);
  gardenPrefs.end();
}


void setup() {
  Serial.begin(115200);
  pinMode(14,INPUT);
   esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.println(reason);
  setenv("TZ", "ICT-7", 1);  
  dfSerial.begin(9600, SERIAL_8N1, 16, 17);
  if (!dfplayer.begin(dfSerial)) {
  Serial.println("ไม่พบ DFPlayer!");
}

  Serial.println("DFPlayer พร้อมใช้งาน");

  dfplayer.volume(100);

  delay(600);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  pinMode(ENC_SW, INPUT);
  tzset();


  if (loadWiFiFromNVS(savedSSID, savedPASS)) {
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    Serial.println(savedSSID.c_str());
    Serial.println(savedPASS.c_str());
  }


  loadGardensFromNVS(allGardens, gardenCount);
  loadRiceColorsFromNVS(riceColors, riceColorCount);

  encoder.begin();
  gpsSerial.begin(GPSBaud);

  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);

  lv_init();

  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 240 * 80);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 320;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_ENCODER;
  indev_drv.read_cb = encoder_read;
  encoder_indev = lv_indev_drv_register(&indev_drv);

  ui_init();


  group_loginwifi = lv_group_create();
  group_selectwifi = lv_group_create();
  group_menu = lv_group_create();
  group_color = lv_group_create();
  group_line = lv_group_create();
  group_smart = lv_group_create();
  group_garden = lv_group_create();
  group_gardenname = lv_group_create();
  group_nogps = lv_group_create();
  group_nogpsgarden = lv_group_create();
  updateGardenPointLabel();

  lv_obj_add_event_cb(ui_RefreshWifiBtn, scanwifi_btn_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_add_event_cb(ui_WiFiBtn1, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn2, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn3, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn4, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn5, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn6, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn7, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn8, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn9, wifi_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_WiFiBtn10, wifi_btn_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_add_event_cb(ui_Keyboard1, keyboard_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_CaptureButton, capture_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_SaveButton, color_save_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_SaveGarden_Btn, save_garden_btn_event_cb, LV_EVENT_PRESSED, NULL);

  lv_obj_add_event_cb(ui_KeyboardGardenName, KeyboardGardenName_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_AddGarden, add_garden_btn_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_add_event_cb(ui_GardenDeleteBtn1, garden_del_btn_event_cb, LV_EVENT_PRESSED, (void*)0);
  lv_obj_add_event_cb(ui_GardenDeleteBtn2, garden_del_btn_event_cb, LV_EVENT_PRESSED, (void*)1);
  lv_obj_add_event_cb(ui_GardenDeleteBtn3, garden_del_btn_event_cb, LV_EVENT_PRESSED, (void*)2);
  lv_obj_add_event_cb(ui_GardenDeleteBtn4, garden_del_btn_event_cb, LV_EVENT_PRESSED, (void*)3);
  lv_obj_add_event_cb(ui_GardenDeleteBtn5, garden_del_btn_event_cb, LV_EVENT_PRESSED, (void*)4);

  lv_obj_add_event_cb(ui_CaptureCloseBtn, captureclose_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_CaptureCloseBtn1, gardenclose_btn_event_cb, LV_EVENT_ALL, NULL);


  lv_group_add_obj(group_menu, ui_Colorbutton);
  lv_group_add_obj(group_menu, ui_Cloudbutton);
  lv_group_add_obj(group_menu, ui_Gardenbutton);
  lv_group_add_obj(group_menu, ui_Wifibutton);
  lv_group_add_obj(group_menu, ui_Guidebutton);
  
  lv_indev_set_group(encoder_indev, group_menu);
  lv_group_focus_obj(ui_Colorbutton);

  lv_group_add_obj(group_loginwifi, ui_Button6);
  lv_group_add_obj(group_loginwifi, ui_Keyboard1);


  lv_group_add_obj(group_selectwifi, ui_RefreshWifiBtn);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn1);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn2);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn3);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn4);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn5);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn6);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn7);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn8);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn9);
  lv_group_add_obj(group_selectwifi, ui_WiFiBtn10);

  lv_group_add_obj(group_selectwifi, ui_Button1);

  
  lv_group_add_obj(group_color, ui_CaptureButton);
  lv_group_add_obj(group_color, ui_SaveButton);
  lv_group_add_obj(group_color, ui_Button7);
  
  
  lv_group_add_obj(group_nogps, ui_CaptureCloseBtn);
  lv_group_add_obj(group_nogpsgarden, ui_CaptureCloseBtn1);


  lv_group_add_obj(group_garden, ui_Button5);
  lv_group_add_obj(group_garden, ui_AddGarden);

  lv_group_add_obj(group_gardenname, ui_SaveGarden_Btn);

  lv_group_add_obj(group_smart, ui_Button4);
  
  lv_group_add_obj(group_line, ui_Button3);

  updateWiFiButtons();
  showGardensInUI();
  lv_timer_handler();
  uiBattery();

}
unsigned long lastLVGL = 0;
const int interval = 5; 
const unsigned long syncInterval = 50000; 
const unsigned long UpdateGPSInterval = 5000; 
const unsigned long AutoWifiInterval = 5000; 

unsigned long lastGPS = 0;
unsigned long lastUpdateGPS= 0;
unsigned long lastSyncTime = 0;
unsigned long lastWifiTime = 0;
static wl_status_t lastStatus = WL_DISCONNECTED;
wl_status_t nowStatus = WiFi.status();
static unsigned long lastBattery = 0;
static unsigned long lastSyncUI = 0;


lv_obj_t* last_screen = NULL;
void loop() {
  unsigned long now = millis();
  lv_obj_t *current_screen = lv_scr_act();
  bool screen_changed = (current_screen != last_screen);

  if (now - lastGPS > 200) {
    lastGPS = now;

    while (gpsSerial.available() > 0) {
      gps.encode(gpsSerial.read());
    }
  }
  if (screen_changed) {

    if (current_screen == ui_Menu) {
      lv_indev_set_group(encoder_indev, group_menu);
      lv_group_focus_obj(ui_Colorbutton);
      if (percentage <= 20) dfplayer.play(3);

    } else if (current_screen == ui_SelectWifi) {
      lv_indev_set_group(encoder_indev, group_selectwifi);

    } else if (current_screen == ui_LoginWifi) {
      lv_indev_set_group(encoder_indev, group_loginwifi);

    } else if (current_screen == ui_Line) {
      lv_indev_set_group(encoder_indev, group_line);
      dfplayer.play(8);

    } else if (current_screen == ui_Smart) {
      lv_indev_set_group(encoder_indev, group_smart);
      dfplayer.play(8);

    } else if (current_screen == ui_Garden) {


    } else if (current_screen == ui_Capture) {

    }
  }

  // ===== Capture screen =====
  if (current_screen == ui_Capture) {
    if (lv_obj_has_flag(ui_Capturecontain, LV_OBJ_FLAG_HIDDEN)) {
      lv_indev_set_group(encoder_indev, group_color);
    }
    updateGPS();
  }
  if (current_screen == ui_Garden){
    if (lv_obj_has_flag(ui_Gardencontain, LV_OBJ_FLAG_HIDDEN)) {
      lv_indev_set_group(encoder_indev, group_garden);
    }
  }
  // ===== Menu screen =====
  if (current_screen == ui_Menu) {
    if (gps.location.isValid()) {
      lv_obj_clear_flag(ui_Gpsiconon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_Gpsiconoff, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ui_Gpsiconon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_Gpsiconoff, LV_OBJ_FLAG_HIDDEN);
    }


    if (now - lastSyncUI > 2000) {
      lastSyncUI = now;
      updateSyncStatusUI();
    }
    // Sync
    if (now - lastSyncTime >= syncInterval) {
      lastSyncTime = now;

      if (WiFi.status() == WL_CONNECTED) {
        debugKeys();
        if (current_screen == ui_Menu && !isSyncing) {
          sendAndClearRiceData();
        }
      } else {
        dfplayer.play(7);
      }
    }
    if (WiFi.status() != WL_CONNECTED){
      if (now - lastWifiTime >= AutoWifiInterval){
        lastWifiTime = now;
        if (loadWiFiFromNVS(savedSSID, savedPASS)) {
          WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
          Serial.println(savedSSID.c_str());
          Serial.println(savedPASS.c_str());
        }
      }

    }else{

    }
    
    // WiFi icon (update เฉพาะตอนเปลี่ยน)


    if (nowStatus != lastStatus) {
      if (nowStatus == WL_CONNECTED) {
        lv_obj_clear_flag(ui_Wiconon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_Wiconoff, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(ui_Wiconon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_Wiconoff, LV_OBJ_FLAG_HIDDEN);
      }
      lastStatus = nowStatus;
    }
    
  }

  // ===== LVGL tick =====
  if (now - lastLVGL >= interval) {
    lastLVGL = now;
    lv_timer_handler();

  }
  if (now - lastBattery >= 1000) { // 20 วินาที
    lastBattery = now;
    uiBattery();
  }

  last_screen = current_screen;

 
  delay(1);
}