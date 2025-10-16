#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ui.h>
#include <Adafruit_TCS34725.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ====== WiFi ======
const char* ssid = "FK Room";
const char* password = "0954852953";

// ====== API URLs ======
String apiColorUrl = "http://192.168.0.102:3000/api/upload_color";
String apiPointUrl = "http://192.168.0.102:3000/api/upload_point";

// ====== GPS ======
static const int RXPin = 16, TXPin = 19;
static const uint32_t GPSBaud = 9600;
TinyGPSPlus gps;
SoftwareSerial gpsSerial(RXPin, TXPin);

// ====== Color sensor ======
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_1X);
float rScale = 0.5870646;
float gScale = 0.85093167;
float bScale = 0.2727272;

// ====== TFT/Touch ======
static const uint16_t screenWidth  = 480;
static const uint16_t screenHeight = 320;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * screenHeight / 10 ];
TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);

bool activateFlag;
bool prevPlus = false;
bool prevMinus = false;
int garden = 1;

// ====== NVS ======
Preferences prefs;

// ====== Current measurements ======
float currentLat = 0.0;
float currentLng = 0.0;
float currentR = 0.0;
float currentG = 0.0;
float currentB = 0.0;

// ====== Garden boundaries (ปรับอัตโนมัติหลังจาก set point ทั้ง 4 จุด) ======
float gardenLatNW = 0;
float gardenLngNW = 0;
float gardenLatSE = 0;
float gardenLngSE = 0;

// ====== LVGL helpers ======
#if LV_USE_LOG != 0
void my_print(const char * buf){ Serial.printf(buf); Serial.flush(); }
#endif

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p){
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data){
    uint16_t touchX = 0, touchY = 0;
    bool touched = tft.getTouch(&touchX, &touchY, 600);
    if (!touched) data->state = LV_INDEV_STATE_REL;
    else {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = screenWidth - touchX;
        data->point.y = screenHeight - touchY;
    }
}

// ====== Update sensors ======
void updateRGB(){
  uint16_t r_raw, g_raw, b_raw, c;
  tcs.getRawData(&r_raw, &g_raw, &b_raw, &c);

  float red   = min(255, (int)(r_raw * rScale));
  float green = min(255, (int)(g_raw * gScale));
  float blue  = min(255, (int)(b_raw * bScale));

  char buf[16];
  sprintf(buf,"%d", (int)red); lv_label_set_text(ui_RValue, buf);
  sprintf(buf,"%d", (int)green); lv_label_set_text(ui_GValue, buf);
  sprintf(buf,"%d", (int)blue); lv_label_set_text(ui_BValue, buf);

  lv_color_t color = lv_color_make((uint8_t)red,(uint8_t)green,(uint8_t)blue);
  lv_obj_set_style_bg_color(ui_RGBBox,color,LV_PART_MAIN);

  currentR = red; currentG = green; currentB = blue;
}

void updateGPS(){
  if(gps.location.isValid()){
    currentLat = gps.location.lat();
    currentLng = gps.location.lng();
  }
  char buf[24];
  sprintf(buf,"%.6f",currentLat); lv_label_set_text(ui_LatitudeValue,buf);
  sprintf(buf,"%.6f",currentLng); lv_label_set_text(ui_LongtitudeValue,buf);
}

// ====== Garden value ======
void updateGarden(int amount){
  if(amount<0 && garden<=1) return;
  garden += amount;
  if(garden<0) garden=1;
  char buf[8]; sprintf(buf,"%d",garden); lv_label_set_text(ui_GardenValue,buf);
}

// ====== Grid ======
int getGridIndex(float lat, float lng, float latNW, float lngNW, float latSE, float lngSE){
  float latStep = (latNW - latSE) / 10.0;
  float lngStep = (lngSE - lngNW) / 10.0;
  int row = (int)((latNW - lat) / latStep);
  int col = (int)((lng - lngNW) / lngStep);
  row = constrain(row,0,9);
  col = constrain(col,0,9);
  return row * 10 + col + 1;
}

// ====== NVS helpers ======
void appendPointToNVS(int pointNum, int gardenID, float lat, float lng){
  prefs.begin("points_log", false);
  String existing = prefs.getString("log","[]");
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();

  for(int i = arr.size()-1; i >= 0; i--){
    JsonObject obj = arr[i].as<JsonObject>();
    if(obj["garden_id"] == gardenID && obj["point_no"] == pointNum){
      arr.remove(i);
    }
  }

  JsonObject item = arr.createNestedObject();
  item["garden_id"] = gardenID;
  item["point_no"] = pointNum;
  item["latitude"] = lat;
  item["longitude"] = lng;

  String out; serializeJson(doc, out);
  prefs.putString("log", out);
  prefs.end();
}

void calculateGardenBoundaries(){
  prefs.begin("points_log", true);
  String existing = prefs.getString("log","[]");
  prefs.end();

  DynamicJsonDocument doc(4096);
  if(deserializeJson(doc, existing) != DeserializationError::Ok) return;
  JsonArray arr = doc.as<JsonArray>();

  if(arr.size() < 4) return; // ต้องมีครบ 4 จุดก่อน

  float minLat = 999, maxLat = -999, minLng = 999, maxLng = -999;
  for(JsonObject obj : arr){
    if(obj["garden_id"] == garden){
      float lat = obj["latitude"];
      float lng = obj["longitude"];
      if(lat < minLat) minLat = lat;
      if(lat > maxLat) maxLat = lat;
      if(lng < minLng) minLng = lng;
      if(lng > maxLng) maxLng = lng;
    }
  }

  gardenLatNW = maxLat;
  gardenLngNW = minLng;
  gardenLatSE = minLat;
  gardenLngSE = maxLng;

  Serial.printf("✅ Updated boundaries Garden %d: NW(%.6f, %.6f) SE(%.6f, %.6f)\n",
    garden, gardenLatNW, gardenLngNW, gardenLatSE, gardenLngSE);
}

// ====== Save Point ======
void savePoint(int pointNum){
  if(!gps.location.isValid()){
    Serial.println("No GPS fix — can't save point!");
    return;
  }
  float lat = gps.location.lat();
  float lng = gps.location.lng();
  appendPointToNVS(pointNum,garden,lat,lng);
  Serial.printf("Saved Point %d Garden %d: %.6f,%.6f\n",pointNum,garden,lat,lng);
  calculateGardenBoundaries();
}

// ====== Color ======
void appendColorToNVS(float r,float g,float b,float lat,float lng,unsigned long ts){
  prefs.begin("color_log", false);
  String existing = prefs.getString("log","[]");
  DynamicJsonDocument doc(8192); 
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();

  int cellIndex = getGridIndex(lat, lng, gardenLatNW, gardenLngNW, gardenLatSE, gardenLngSE);

  for(int i = arr.size()-1; i >= 0; i--){
    JsonObject obj = arr[i].as<JsonObject>();
    if(obj["garden_id"] == garden && obj["cell_index"] == cellIndex){
      arr.remove(i);
    }
  }

  JsonObject item = arr.createNestedObject();
  item["device_id"] = "esp32s3_01";
  item["garden_id"] = garden;
  item["latitude"] = lat;
  item["longitude"] = lng;
  item["r"] = (int)r;
  item["g"] = (int)g;
  item["b"] = (int)b;
  item["ts"] = ts;
  item["cell_index"] = cellIndex;

  String out; 
  serializeJson(doc, out);
  prefs.putString("log", out);
  prefs.end();
}

void saveColorData() {
  if (!gps.location.isValid()) {
    showStatusMessage("No GPS fix — Can't save color");
    return;
  }

  unsigned long ts = millis();
  appendColorToNVS(currentR, currentG, currentB, currentLat, currentLng, ts);
  showStatusMessage("Color saved successfully!");
}


// ====== Upload ======
bool uploadSinglePoint(JsonObject obj){
  if(WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http; 
  http.begin(apiPointUrl.c_str()); 
  http.setTimeout(2000);
  http.addHeader("Content-Type","application/json");
  String payload; serializeJson(obj,payload);
  int code = http.POST(payload);
  http.end();
  
  return (code > 0);
}

bool uploadSingleColor(JsonObject obj){
  if(WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http; 
  http.begin(apiColorUrl.c_str()); 
  http.setTimeout(2000);
  http.addHeader("Content-Type","application/json");
  
  float lat = obj["latitude"];
  float lng = obj["longitude"];
  obj["cell_index"] = getGridIndex(lat,lng,gardenLatNW,gardenLngNW,gardenLatSE,gardenLngSE);

  String payload; serializeJson(obj,payload);
  int code = http.POST(payload);
  http.end();
  
  return (code > 0);
}

void uploadQueuedData(){
  prefs.begin("points_log", true);
  String existingPoints = prefs.getString("log","[]");
  prefs.end();
  DynamicJsonDocument docPoints(4096);
  if(deserializeJson(docPoints, existingPoints) != DeserializationError::Ok) return;
  JsonArray arrPoints = docPoints.as<JsonArray>();
  for(int i=0;i<arrPoints.size();i++) uploadSinglePoint(arrPoints[i]);

  prefs.begin("color_log", true);
  String existingColors = prefs.getString("log","[]");
  prefs.end();
  DynamicJsonDocument docColors(8192);
  if(deserializeJson(docColors, existingColors) != DeserializationError::Ok) return;
  JsonArray arrColors = docColors.as<JsonArray>();
  for(int i=0;i<arrColors.size();i++) uploadSingleColor(arrColors[i]);
}

// ====== TextArea update ======
void updateDataValueTextArea(){
  prefs.begin("points_log", true);
  String points = prefs.getString("log","[]");
  prefs.end();

  prefs.begin("color_log", true);
  String colors = prefs.getString("log","[]");
  prefs.end();

  String combined = "{\n\"points\": " + points + ",\n\"colors\": " + colors + "\n}";
  lv_textarea_set_text(ui_DataValue, combined.c_str());
  lv_obj_invalidate(ui_DataValue);
}

// ====== Clear Data ======
void clearAllData(){
  prefs.begin("points_log", false); prefs.clear(); prefs.end();
  prefs.begin("color_log", false); prefs.clear(); prefs.end();
  updateDataValueTextArea();
  Serial.println("All NVS data cleared");
}
void showStatusMessage(const char* message) {
  // แสดงข้อความใน TextArea ที่มีอยู่ (ไม่ยุ่งกับไฟล์ ui)
  lv_textarea_set_text(ui_DataValue, message);
  lv_obj_invalidate(ui_DataValue);
  Serial.println(message);
}

// ====== Setup ======
void setup(){
  Serial.begin(115200);
  uint16_t calData[5]={247,3623,307,3306,7}; tft.setTouch(calData);
  lv_init(); tft.begin(); tft.setRotation(3);
  lv_disp_draw_buf_init(&draw_buf,buf,NULL,screenWidth*screenHeight/10);
  static lv_disp_drv_t disp_drv; lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res=screenWidth; disp_drv.ver_res=screenHeight;
  disp_drv.flush_cb=my_disp_flush; disp_drv.draw_buf=&draw_buf; lv_disp_drv_register(&disp_drv);
  static lv_indev_drv_t indev_drv; lv_indev_drv_init(&indev_drv);
  indev_drv.type=LV_INDEV_TYPE_POINTER; indev_drv.read_cb=my_touchpad_read; lv_indev_drv_register(&indev_drv);
  ui_init();
  gpsSerial.begin(GPSBaud);
  if(!tcs.begin()){ Serial.println("No TCS34725 found"); while(1); }
  prefs.begin("init",false); prefs.end();
  WiFi.begin(ssid,password);
  updateDataValueTextArea();
}

// ====== Loop ======
unsigned long lastUploadAttempt = 0;
const unsigned long uploadInterval = 30000; // 30 วินาที

void loop(){
  activateFlag = lv_obj_has_state(ui_ActivateButton, LV_STATE_CHECKED); 
  if(activateFlag) updateRGB();

  bool currPlus = lv_obj_has_state(ui_Addbutton, LV_STATE_PRESSED);
  bool currMinus = lv_obj_has_state(ui_Minusbutton, LV_STATE_PRESSED);
  bool SaveColor = lv_obj_has_state(ui_ColorSaveButton, LV_STATE_PRESSED);
  bool SaveButton1 = lv_obj_has_state(ui_Point1SaveButton, LV_STATE_PRESSED);
  bool SaveButton2 = lv_obj_has_state(ui_Point2SaveButton, LV_STATE_PRESSED);
  bool SaveButton3 = lv_obj_has_state(ui_Point3SaveButton, LV_STATE_PRESSED);
  bool SaveButton4 = lv_obj_has_state(ui_Point4SaveButton, LV_STATE_PRESSED);
  bool ClearData = lv_obj_has_state(ui_ClearDataButton, LV_STATE_PRESSED);

  if(currPlus && !prevPlus) updateGarden(1);
  if(currMinus && !prevMinus) updateGarden(-1);
  prevPlus=currPlus; prevMinus=currMinus;

  if(gps.location.isUpdated()) updateGPS();

  static bool prevSave1=false,prevSave2=false,prevSave3=false,prevSave4=false,prevSaveColor=false,prevClear=false;
  if(SaveButton1 && !prevSave1){ savePoint(1); updateDataValueTextArea(); }
  if(SaveButton2 && !prevSave2){ savePoint(2); updateDataValueTextArea(); }
  if(SaveButton3 && !prevSave3){ savePoint(3); updateDataValueTextArea(); }
  if(SaveButton4 && !prevSave4){ savePoint(4); updateDataValueTextArea(); }
  if(SaveColor && !prevSaveColor){ saveColorData(); updateDataValueTextArea(); }
  if(ClearData && !prevClear) clearAllData();
  prevSave1=SaveButton1; prevSave2=SaveButton2; prevSave3=SaveButton3; prevSave4=SaveButton4;
  prevSaveColor=SaveColor; prevClear=ClearData;

  if(WiFi.status() == WL_CONNECTED && millis()-lastUploadAttempt > uploadInterval){
      lastUploadAttempt = millis();
      uploadQueuedData();
      updateDataValueTextArea();
  }

  lv_timer_handler(); 
  delay(5);
}
