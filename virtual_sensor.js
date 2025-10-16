// File: virtual_sensor.js
const mqtt = require('mqtt');

// --- 1. ตั้งค่า ---
const MQTT_BROKER_URL = 'mqtt://broker.hivemq.com';
const MQTT_TOPIC = 'smartfarm/data/update'; // ชื่อช่องทางสื่อสารของเรา

console.log('กำลังเริ่มต้น "เซ็นเซอร์จำลอง"...');
const client = mqtt.connect(MQTT_BROKER_URL);

// --- 2. ข้อมูลจำลองของจุดเซ็นเซอร์ (อัปเดตเป็น 4 จุด บนที่นาตัวอย่าง) ---
const SENSOR_POINTS = [
  // สวนที่ 1 มี 2 จุด
  { garden_id: 1, point_id: 1, lat: 14.4755, lng: 100.1180, color: "green" },
  { garden_id: 1, point_id: 2, lat: 14.4758, lng: 100.1184, color: "yellow" },
  
  // สวนที่ 2 มี 2 จุด
  { garden_id: 2, point_id: 1, lat: 14.4765, lng: 100.1190, color: "green" },
  { garden_id: 2, point_id: 2, lat: 14.4768, lng: 100.1195, color: "red" },
];


let currentSensorIndex = 0;

// --- 3. เมื่อเชื่อมต่อกับ "บุรุษไปรษณีย์" สำเร็จ ---
client.on('connect', () => {
  console.log('[เซ็นเซอร์จำลอง] เชื่อมต่อกับ MQTT Broker สำเร็จ!');

  // --- 4. เริ่มส่งข้อมูลทุกๆ 5 วินาที ---
  setInterval(() => {
    const sensorData = SENSOR_POINTS[currentSensorIndex];
    const payload = JSON.stringify(sensorData);

    // ส่งข้อมูล!
    client.publish(MQTT_TOPIC, payload, () => {
      console.log(`[เซ็นเซอร์จำลอง] ส่งข้อมูล: ${payload}`);
    });

    // เลื่อนไปเซ็นเซอร์ตัวถัดไปเพื่อส่งในรอบหน้า
    currentSensorIndex = (currentSensorIndex + 1) % SENSOR_POINTS.length;
  }, 5000); // 5000 มิลลิวินาที = 5 วินาที
});
