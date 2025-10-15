#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ui.h>
#include <Adafruit_TCS34725.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

static const int RXPin = 16, TXPin = 19;
static const uint32_t GPSBaud = 9600;

TinyGPSPlus gps;

SoftwareSerial gpsSerial(RXPin, TXPin);

Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_600MS, TCS34725_GAIN_1X);

float rScale = 0.5870646;
float gScale = 0.85093167;
float bScale = 0.2727272;

bool activateFlag;
int garden = 0;

static const uint16_t screenWidth  = 480;
static const uint16_t screenHeight = 320;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * screenHeight / 10 ];

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight); /* TFT instance */

#if LV_USE_LOG != 0
void my_print(const char * buf)
{
    Serial.printf(buf);
    Serial.flush();
}
#endif

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

/* Touchpad read */
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    uint16_t touchX = 0, touchY = 0;
    bool touched = tft.getTouch(&touchX, &touchY, 600);

    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = screenWidth - touchX;
        data->point.y = screenHeight - touchY;

        Serial.print("Touch X: ");
        Serial.println(touchX);
        Serial.print("Touch Y: ");
        Serial.println(touchY);
    }
}

void updateRGB()
{
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  Serial.println(r);
  Serial.println(g);
  Serial.println(b);
  float red   = min(255, (int)(r * rScale));
  float green = min(255, (int)(g * gScale));
  float blue  = min(255, (int)(b * bScale));
  float chl   = 26.6 + (0.449*red) - (0.0588*green) - (0.381*blue);
  
  float targetR[] = {118, 104, 85, 60, 47, 31};
  float targetG[] = {137, 125, 122, 97, 85, 66};
  float targetB[] = {24,  23,  38, 35, 46, 34};

  float L[6];
  float minDist = 99999;
  int minIndex = -1;

  for (int i = 0; i < 6; i++) {
    L[i] = sqrt(
      pow((targetR[i] - red), 2) +
      pow((targetG[i] - green), 2) +
      pow((targetB[i] - blue), 2)
    );

    if (L[i] < minDist) {
      minDist = L[i];
      minIndex = i;
    }
  }

  char buf[8];

  sprintf(buf, "%d", (int)red);
  lv_label_set_text(ui_RValue, buf);

  sprintf(buf, "%d", (int)green);
  lv_label_set_text(ui_GValue, buf);

  sprintf(buf, "%d", (int)blue);
  lv_label_set_text(ui_BValue, buf);

  sprintf(buf, "%.2f", (float)chl);
  lv_label_set_text(ui_ChlValue, buf);

  sprintf(buf, "%d", (int)minIndex + 1);
  lv_label_set_text(ui_GroupValue, buf);

  lv_color_t color = lv_color_make((uint8_t)red, (uint8_t)green, (uint8_t)blue);
  lv_obj_set_style_bg_color(ui_RGBBox, color, LV_PART_MAIN);
}
void updateGPS()
{
    float lat = gps.location.lat();
    float lng = gps.location.lng();
    int year =  gps.date.year();
    int month = gps.date.month();
    int day = gps.date.day();
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();
    char buf[8];

    sprintf(buf, "%.6f", (float)lat);
    lv_label_set_text(ui_LatitudeValue, buf);

    sprintf(buf, "%.6f", (float)lng);
    lv_label_set_text(ui_LongtitudeValue, buf);
}
void updateGarden(amount)
{
  if (amount < 0) return;
  garden += amount;
  char buf[8];
  sprintf(buf, "%d", (int)garden);
  lv_label_set_text(ui_GardenValue, buf);
}

void setup()
{
    Serial.begin(115200);

    uint16_t calData[5] = {247, 3623, 307, 3306, 7};
    tft.setTouch(calData);

    lv_init();
    
#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    tft.begin();
    tft.setRotation(3);

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    ui_init();
    
    gpsSerial.begin(GPSBaud);
    Serial.println("Setup done");
    
    if(tcs.begin())
        Serial.println("TCS34725 found");
    else
    {
        Serial.println("No TCS34725 found ... check your connections");
        while(1);
    }
}

void loop() {
    activateFlag = lv_obj_has_state(ui_ActivateButton, LV_STATE_CHECKED);
    if (activateFlag) updateRGB();

    gpsplusbutton = lv_obj_has_state(ui_Addbutton, LV_STATE_PRESSED)
    gpsminusbutton = lv_obj_has_state(ui_Minusbutton, LV_STATE_PRESSED)
    if (gpsplusbutton) updateGarden(1);
    if (gpsminusbutton) updateGarden(-1);

    while (gpsSerial.available() > 0) {
        char c = gpsSerial.read();
        gps.encode(c);
    }

    if (gps.location.isUpdated()) {
        updateGPS();
    }

    lv_timer_handler();
    delay(5);
}
