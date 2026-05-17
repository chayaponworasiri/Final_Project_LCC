#include "ApiSender.h"

ApiSender::ApiSender(const char* server, const char* machineId) {
  _server = server;
  _machineId = machineId;
}

void ApiSender::sendPlot(const PlotData& plot) {
  HTTPClient http;
  http.begin(String(_server) + "/api/plot");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["machine_id"] = _machineId;
  doc["plotName"]   = plot.plotName;
  doc["plotPoint"] = plot.plotPoint;
  doc["lat"]       = plot.lat;
  doc["lng"]       = plot.lng;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.println("Plot POST → " + payload);
  Serial.println("Response: " + String(code));

  http.end();
}

bool ApiSender::sendRiceColor(const RiceColorData& color) {
  HTTPClient http;
  String url = String(_server) + "/api/ricecolor"; // เช็ค Path ตรงนี้ให้ดี
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["machine_id"]    = _machineId;
  doc["measure_point"] = color.measure_point;
  doc["r"]             = color.r;
  doc["g"]             = color.g;
  doc["b"]             = color.b;
  doc["lat"]           = color.lat;
  doc["lng"]           = color.lng;
  doc["lcc"]           = color.lcc;
  doc["measured_at"]   = color.measured_at; 

  String payload;
  serializeJson(doc, payload);

  // สั่ง POST แค่ครั้งเดียวพอครับ!
  int httpResponseCode = http.POST(payload);
  
  Serial.print("RiceColor POST → ");
  Serial.println(payload);
  Serial.print("Response: ");
  Serial.println(httpResponseCode);
  
  http.end();
  http.setTimeout(3000);    
  http.setReuse(false);   

  // คืนค่า true เฉพาะเมื่อ Server ตอบรับว่าได้รับข้อมูลแล้ว
  return (httpResponseCode == 200 || httpResponseCode == 201);
}

void ApiSender::deleteGarden(const char* plotName) {
  HTTPClient http;
  http.begin(String(_server) + "/api/plot/delete");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["machine_id"] = _machineId;
  doc["plotName"]   = plotName;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.println("DELETE Plot → " + payload);
  Serial.println("Response: " + String(code));

  http.end();
}

void ApiSender::syncGardens(const char** gardenList, int count) {
  HTTPClient http;
  http.begin(String(_server) + "/api/plot/sync");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["machine_id"] = _machineId;
  JsonArray arr = doc.createNestedArray("gardens");

  for (int i = 0; i < count; i++) {
    arr.add(gardenList[i]);
  }

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.println("SYNC Gardens → " + payload);
  Serial.println("Response: " + String(code));

  http.end();
}

