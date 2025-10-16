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
const char* ssid = "mai-mee-net-rer-ja";
const char* password = "Hisatang888";

// ====== API URLs ======
String apiColorUrl = "http://192.168.245.189:3000/api/upload_color";
String apiPointUrl = "http://192.168.245.189:3000/api/upload_point";

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
int garden = 0;

// ====== NVS ======
Preferences prefs;

// ====== Current measurements ======
float currentLat = 0.0;
float currentLng = 0.0;
float currentR = 0.0;
float currentG = 0.0;
float currentB = 0.0;

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

  float targetR[] = {118,104,85,60,47,31};
  float targetG[] = {137,125,122,97,85,66};
  float targetB[] = {24,23,38,35,46,34};
  float minDist = 99999; int minIndex=-1;
  for(int i=0;i<6;i++){
    float d = sqrt(pow(targetR[i]-red,2)+pow(targetG[i]-green,2)+pow(targetB[i]-blue,2));
    if(d<minDist){minDist=d; minIndex=i;}
  }

  char buf[16];
  sprintf(buf,"%d", (int)red); lv_label_set_text(ui_RValue, buf);
  sprintf(buf,"%d", (int)green); lv_label_set_text(ui_GValue, buf);
  sprintf(buf,"%d", (int)blue); lv_label_set_text(ui_BValue, buf);
  sprintf(buf,"%d", (int)minIndex+1); lv_label_set_text(ui_GroupValue, buf);

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
  if(amount<0 && garden<=0) return;
  garden += amount;
  if(garden<0) garden=0;
  char buf[8]; sprintf(buf,"%d",garden); lv_label_set_text(ui_GardenValue,buf);
}

// ====== Save Points ======
void uploadPointData(int pointNum, int gardenID, float lat, float lng){
  if(WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http; http.begin(apiPointUrl.c_str()); http.addHeader("Content-Type","application/json");
  StaticJsonDocument<256> doc;
  doc["device_id"]="esp32s3_01"; doc["garden_id"]=gardenID;
  doc["point_no"]=pointNum; doc["latitude"]=lat; doc["longitude"]=lng;
  doc["timestamp"]=millis();
  String jsonData; serializeJson(doc,jsonData);
  int code = http.POST(jsonData); http.end();
  if(code>0) Serial.printf("Uploaded Point %d Garden %d\n",pointNum,gardenID);
}

void savePoint(int pointNum){
//   if(!gps.location.isValid()) { Serial.println("No GPS fix"); return; }
  prefs.begin("garden", false);
  float latsim[] = {14.044,14.044,14.042,14.042};
  float lngsim[] = {100.610,100.615,100.615,100.610};
  float lat = latsim[pointNum-1];
  float lng = lngsim[pointNum-1];
//   float lat = gps.location.lat();
//   float lng = gps.location.lng();
  int gardenID = garden;
  String keyLat = "point"+String(pointNum)+"_lat";
  String keyLng = "point"+String(pointNum)+"_lng";
  prefs.putFloat(keyLat.c_str(),lat);
  prefs.putFloat(keyLng.c_str(),lng);
  prefs.putInt("garden_id",gardenID);
  prefs.end();
  Serial.printf("Saved Point %d Garden %d: %.6f,%.6f\n",pointNum,gardenID,lat,lng);
  if(WiFi.status()==WL_CONNECTED) uploadPointData(pointNum,gardenID,lat,lng);
}

// ====== Save Color ======
void appendColorToNVS(float r,float g,float b,float lat,float lng,unsigned long ts){
  prefs.begin("color_log", false);
  String existing = prefs.getString("log","[]");
  DynamicJsonDocument doc(8192); deserializeJson(doc,existing); JsonArray arr=doc.as<JsonArray>();
  JsonObject item = arr.createNestedObject();
  item["device_id"]="esp32s3_01"; item["garden_id"]=garden;
  item["latitude"]=lat; item["longitude"]=lng;
  item["r"]=(int)r; item["g"]=(int)g; item["b"]=(int)b; item["ts"]=ts;
  String out; serializeJson(doc,out);
  prefs.putString("log",out); prefs.end();
}

void uploadSingleColor(JsonObject obj){
  if(WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http; http.begin(apiColorUrl.c_str()); http.addHeader("Content-Type","application/json");
  String payload; serializeJson(obj,payload);
  int code = http.POST(payload); http.end();
  if(code>0) Serial.println("Uploaded color data");
}

void uploadQueuedColors(){
  prefs.begin("color_log", true);
  String existing = prefs.getString("log","[]"); prefs.end();
  DynamicJsonDocument doc(8192); deserializeJson(doc,existing); JsonArray arr=doc.as<JsonArray>();
  for(size_t i=0;i<arr.size();i++){ uploadSingleColor(arr[i]); }
}

// Save Color button
void saveColorData(){
  unsigned long ts=millis();
  appendColorToNVS(currentR,currentG,currentB,currentLat,currentLng,ts);
  if(WiFi.status()==WL_CONNECTED) uploadQueuedColors();
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
}

// ====== Loop ======
void loop(){
  activateFlag = lv_obj_has_state(ui_ActivateButton, LV_STATE_CHECKED); if(activateFlag) updateRGB();
  bool currPlus = lv_obj_has_state(ui_Addbutton, LV_STATE_PRESSED);
  bool currMinus = lv_obj_has_state(ui_Minusbutton, LV_STATE_PRESSED);
  bool SaveColor = lv_obj_has_state(ui_ColorSaveButton, LV_STATE_PRESSED);
  bool SaveButton1 = lv_obj_has_state(ui_Point1SaveButton, LV_STATE_PRESSED);
  bool SaveButton2 = lv_obj_has_state(ui_Point2SaveButton, LV_STATE_PRESSED);
  bool SaveButton3 = lv_obj_has_state(ui_Point3SaveButton, LV_STATE_PRESSED);
  bool SaveButton4 = lv_obj_has_state(ui_Point4SaveButton, LV_STATE_PRESSED);

  if(currPlus && !prevPlus) updateGarden(1);
  if(currMinus && !prevMinus) updateGarden(-1);
  prevPlus=currPlus; prevMinus=currMinus;

  while(gpsSerial.available()>0) gps.encode(gpsSerial.read());
  if(gps.location.isUpdated()) updateGPS();

  static bool prevSave1=false,prevSave2=false,prevSave3=false,prevSave4=false,prevSaveColor=false;
  if(SaveButton1 && !prevSave1) savePoint(1);
  if(SaveButton2 && !prevSave2) savePoint(2);
  if(SaveButton3 && !prevSave3) savePoint(3);
  if(SaveButton4 && !prevSave4) savePoint(4);
  if(SaveColor && !prevSaveColor) saveColorData();
  prevSave1=SaveButton1; prevSave2=SaveButton2; prevSave3=SaveButton3; prevSave4=SaveButton4; prevSaveColor=SaveColor;

  static unsigned long lastWifiCheck=0;
  if(millis()-lastWifiCheck>5000){
    lastWifiCheck=millis();
    if(WiFi.status()!=WL_CONNECTED) WiFi.reconnect();
    else uploadQueuedColors();
  }

  lv_timer_handler(); delay(5);
}
