// File: server.js (เวอร์ชันแก้ไข - เข้าใจข้อมูลทีละจุด)

const express = require('express');
const http = require('http' );
const WebSocket = require('ws');
const path = require('path');

const app = express();
const server = http.createServer(app );
const wss = new WebSocket.Server({ server });

// Middleware สำหรับอ่าน JSON body จาก Request
app.use(express.json());

// ===================================================================
//  ส่วนเก็บข้อมูล (เปรียบเสมือนฐานข้อมูลชั่วคราวบน RAM)
// ===================================================================
// โครงสร้าง: gardenBoundaries จะเก็บพิกัดของแต่ละสวน
// ในรูปแบบ { "1": [ {lat, lng}, {lat, lng}, ... ], "2": [ ... ] }
const gardenBoundaries = {};


// --- 1. Endpoint หลัก: ส่งไฟล์ index.html ให้ผู้ใช้ ---
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});


// ===================================================================
//  API Endpoint สำหรับรับข้อมูล "พิกัดมุมสวน" จาก ESP32
// ===================================================================
app.post('/api/upload_point', (req, res) => {
    const data = req.body;
    console.log('[API] ได้รับข้อมูล Point:', data);

    // ตรวจสอบความสมบูรณ์ของข้อมูลที่ส่งมา
    if (data.garden_id === undefined || data.point_no === undefined || data.latitude === undefined || data.longitude === undefined) {
        console.error('[API Error] ข้อมูล Point ที่ได้รับไม่สมบูรณ์:', data);
        return res.status(400).send({ message: 'Incomplete point data received' });
    }

    const { garden_id, point_no, latitude, longitude } = data;

    // ถ้ายังไม่มีข้อมูลของสวนนี้ใน Object ของเรา ให้สร้าง Array ว่างๆ ขึ้นมารอ
    if (!gardenBoundaries[garden_id]) {
        gardenBoundaries[garden_id] = [];
    }

    // เก็บพิกัดใหม่ลงใน Array ของสวนนั้นๆ
    // ลบ 1 เพื่อให้ point_no 1, 2, 3, 4 ไปอยู่ที่ index 0, 1, 2, 3
    gardenBoundaries[garden_id][point_no - 1] = { lat: latitude, lng: longitude };

    console.log(`[เซิร์ฟเวอร์] บันทึก สวน ${garden_id}, จุดที่ ${point_no} สำเร็จ`);
    console.log('[เซิร์ฟเวอร์] ข้อมูลสวนทั้งหมดตอนนี้:', JSON.stringify(gardenBoundaries, null, 2));

    // --- Logic การวาดกรอบ (หัวใจสำคัญ) ---
    const currentGardenPoints = gardenBoundaries[garden_id];
    
    // ตรวจสอบว่าสวนนี้มีข้อมูลครบ 4 จุดที่ถูกต้องหรือยัง
    // ใช้ .filter(p => p) เพื่อกรองค่า empty หรือ undefined ออกไปก่อนนับ
    if (currentGardenPoints && currentGardenPoints.filter(p => p).length === 4) {
        console.log(`[เซิร์ฟเวอร์] สวน ${garden_id} มีครบ 4 จุดแล้ว! กำลังสั่งให้หน้าเว็บวาดกรอบ...`);
        
        // ส่งคำสั่ง (broadcast) ไปให้หน้าเว็บทุกหน้าที่เชื่อมต่ออยู่
        broadcast({
            type: 'create_polygon',
            data: {
                id: `garden${garden_id}`, // ID ของ Polygon บนแผนที่ เช่น "garden1"
                coords: currentGardenPoints
            }
        });
    }

    // ตอบกลับไปหา ESP32 ว่าได้รับข้อมูลเรียบร้อยแล้ว
    res.status(200).send({ message: 'Point received and processed' });
});


// --- API Endpoint สำหรับรับข้อมูล "ค่าสี" จาก ESP32 ---
app.post('/api/upload_color', (req, res) => {
    const data = req.body;
    console.log('[API] ได้รับข้อมูล Color:', data);
    
    // TODO: ในอนาคตต้องเพิ่ม Logic การคำนวณว่าพิกัดสีนี้ตกอยู่ในช่องกริดไหน
    // ตอนนี้จะสุ่มไปก่อนเพื่อใช้ในการทดสอบ
    const randomCellIndex = Math.floor(Math.random() * 100);

    // ส่งคำสั่งไปอัปเดตสีในตารางกริดบนหน้าเว็บ
    broadcast({
        type: 'update_grid_cell',
        data: {
            garden_id: data.garden_id,
            cell_index: randomCellIndex,
            color: { r: data.r, g: data.g, b: data.b }
        }
    });

    res.status(200).send({ message: 'Color received successfully' });
});


// --- WebSocket Server: ช่องทางการสื่อสารกับหน้าเว็บ ---
wss.on('connection', ws => {
    console.log('[WebSocket] มีหน้าเว็บใหม่เชื่อมต่อเข้ามา');

    // เมื่อมีหน้าเว็บใหม่เข้ามา ให้ส่งข้อมูลสวนที่มีอยู่แล้วไปให้ทันที
    // เพื่อให้กรอบที่เคยวาดไว้แล้วแสดงผลขึ้นมาเลย ไม่ต้องรอ ESP32 ส่งใหม่
    for (const gardenId in gardenBoundaries) {
        const points = gardenBoundaries[gardenId];
        if (points && points.filter(p => p).length === 4) {
            ws.send(JSON.stringify({
                type: 'create_polygon',
                data: {
                    id: `garden${gardenId}`,
                    coords: points
                }
            }));
        }
    }

    ws.on('close', () => console.log('[WebSocket] หน้าเว็บถูกปิดการเชื่อมต่อ'));
});

// ฟังก์ชันสำหรับส่งข้อมูลไปยังหน้าเว็บทุกหน้าที่เชื่อมต่ออยู่ (Broadcast)
function broadcast(message) {
    const jsonMessage = JSON.stringify(message);
    wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(jsonMessage);
        }
    });
}

// --- เริ่มการทำงานของเซิร์ฟเวอร์ ---
const PORT = 3000;
server.listen(PORT, () => {
    console.log(`[เซิร์ฟเวอร์] เริ่มทำงานที่ http://localhost:${PORT}` );
    console.log(`[API] พร้อมรับข้อมูลจาก ESP32 ที่ /api/upload_point และ /api/upload_color`);
});
