#ifndef API_SENDER_H
#define API_SENDER_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct PlotData {
  const char* plotName;
  int plotPoint;
  float lat;
  float lng;
};

struct RiceColorData {
  int measure_point;
  int r, g, b;
  float lat, lng;
  int lcc;
  uint32_t measured_at;
};

class ApiSender {
public:
  ApiSender(const char* server, const char* machineId);

  void sendPlot(const PlotData& plot);
  bool sendRiceColor(const RiceColorData& color);
  void deleteGarden(const char* plotName);
  void syncGardens(const char** gardenList, int count);
private:
  const char* _server;
  const char* _machineId;
};



#endif
