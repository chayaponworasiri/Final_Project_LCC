#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ui.h>
#include <LovyanGFX.hpp>
#include "EncoderRead.h"
#include <Adafruit_TCS34725.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_600MS, TCS34725_GAIN_1X);

static const int RXPin = 16, TXPin = 19;
static const uint32_t GPSBaud = 115200;
TinyGPSPlus gps;
SoftwareSerial gpsSerial(RXPin, TXPin);

float currentR = 0.0;
float currentG = 0.0;
float currentB = 0.0;
float currentLat = 0.0;
float currentLng = 0.0;
#define ENC_A   18
#define ENC_B   15
#define ENC_SW  10

EncoderRead encoder(ENC_A, ENC_B, ENC_SW);

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
      cfg.freq_read  = 25000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 13;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 21;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs   = 5;
      cfg.pin_rst  = 7;
      cfg.pin_busy = -1;
      cfg.panel_width  = 320;
      cfg.panel_height = 480;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
static int32_t last_counter = 0;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 40];

lv_group_t *group_home = NULL;
lv_group_t *group_lcc  = NULL;

lv_indev_t *encoder_indev = NULL;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixels((uint16_t *)color_p, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

void encoder_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  static int32_t last_enc = 0;
  static uint32_t boot_time = 0;

  if (boot_time == 0) boot_time = millis();

  int32_t counter = encoder.getCounter();
  bool btn_state  = encoder.encBtn();  // pull-up

  if (millis() - boot_time < 500) {
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_REL;
    last_enc = counter;
    return;
  }

  data->enc_diff = counter - last_enc;
  data->state    = btn_state ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

  last_enc = counter;
}
void updateGPS(){
  Serial.print("LAT: ");
  Serial.println(gps.location.lat(), 6);
  Serial.print("LONG: "); 
  Serial.println(gps.location.lng(), 6);
  currentLat = gps.location.lat();
  currentLng = gps.location.lng();
  char buf[24];
  sprintf(buf,"%.10f",currentLat); lv_label_set_text(ui_Lad,buf);
  sprintf(buf,"%.10f",currentLng); lv_label_set_text(ui_Lng,buf);
}

void RGBupdate(){
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);

  float rScale = 0.0918;
  float gScale = 0.1439;
  float bScale = 0.0898;

  float red   = min(r * rScale, 255.0f);
  float green = min(g * gScale, 255.0f);
  float blue  = min(b * bScale, 255.0f);

  float chl = 26.6 + (0.449*red) - (0.0588*green) - (0.381*blue);

  float targetR[] = {153, 117, 85, 76, 63, 55};
  float targetG[] = {192, 157, 122, 111, 90, 78};
  float targetB[] = {57,  47,  37, 36, 32, 29};

  float L[6];
  float minDist = 99999;
  int minIndex = -1;

  for (int i=0;i<6;i++){
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

  int group = minIndex + 1;

  Serial.print(group);


  currentR = red;
  currentG = green;
  currentB = blue;
  char buf[32];
  sprintf(buf, "R:%d G:%d B:%d", (int)red, (int)green, (int)blue); lv_label_set_text(ui_RGB_Label, buf);
  sprintf(buf,"%d",group); lv_label_set_text(ui_Group_Label,buf);
  lv_color_t color = lv_color_make((uint8_t)red, (uint8_t)green, (uint8_t)blue);
  lv_obj_set_style_bg_color(ui_RGBBox, color, LV_PART_MAIN);
}
void capture_btn_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_CLICKED) {
    RGBupdate();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ENC_SW, INPUT_PULLUP);

  encoder.begin();  
  gpsSerial.begin(GPSBaud);

  tft.init();
  tft.setRotation(0);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 320 * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = 320;
  disp_drv.ver_res  = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_ENCODER;
  indev_drv.read_cb = encoder_read;
  encoder_indev = lv_indev_drv_register(&indev_drv);

  ui_init();
  lv_obj_add_event_cb(ui_Capture_Btn, capture_btn_event_cb, LV_EVENT_ALL, NULL);

  // ---- group หน้า Home ----
  group_home = lv_group_create();
  lv_group_add_obj(group_home, ui_LCC_Btn);
  lv_group_add_obj(group_home, ui_GPS_Btn);
  lv_group_add_obj(group_home, ui_WIFI_Btn);
  lv_group_add_obj(group_home, ui_SETTING_Btn);

  // ---- group หน้า LCC ----
  group_lcc = lv_group_create();
  lv_group_add_obj(group_lcc, ui_Exit_Btn);
  lv_group_add_obj(group_lcc, ui_Capture_Btn);

  // เริ่มต้นให้ encoder ใช้ group_home
  lv_indev_set_group(encoder_indev, group_home);
  lv_group_focus_obj(ui_LCC_Btn);

}

void loop() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  lv_obj_t *current_screen = lv_scr_act();
  if (current_screen == ui_LCC) {
      lv_indev_set_group(encoder_indev, group_lcc);

      if (gps.location.isUpdated()) {
        updateGPS();
      }
  }else{
    lv_indev_set_group(encoder_indev, group_home);
  }
  
  lv_timer_handler();
  delay(1);
}
