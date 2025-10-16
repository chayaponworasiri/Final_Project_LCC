// File: server.js (ฉบับสมบูรณ์ - เพิ่ม Logic คำนวณ Grid)

const express = require('express');
const http = require('http' );
const WebSocket = require('ws');
const path = require('path');

const app = express();
const server = http.createServer(app );
const wss = new WebSocket.Server({ server });

app.use(express.json());

// --- ส่วนเก็บข้อมูล (ฐานข้อมูลชั่วคราวบน RAM) ---
const gardenData = {}; // { "1": { coords: [...], created_at: ... }, "2": ... }

// --- Endpoint หลัก: ส่งไฟล์ index.html ---
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// --- API: ดึงข้อมูลสวนทั้งหมดเมื่อเปิดหน้าเว็บ ---
app.get('/api/get_all_gardens', (req, res) => {
    const allGardens = Object.keys(gardenData).map(id => ({
        garden_id: id,
        coords: gardenData[id].coords,
        created_at: gardenData[id].created_at
    }));
    res.status(200).json(allGardens);
});

// --- API: รับข้อมูล Point จาก ESP32 ---
app.post('/api/upload_point', (req, res) => {
    const { garden_id, point_no, latitude, longitude } = req.body;
    console.log(`[API] ได้รับ Point ${point_no} จากสวน ${garden_id}`);

    if (!gardenData[garden_id]) {
        gardenData[garden_id] = { coords: [], created_at: new Date().toISOString() };
    }

    gardenData[garden_id].coords[point_no - 1] = { lat: latitude, lng: longitude };
    const currentPoints = gardenData[garden_id].coords;

    if (currentPoints.filter(p => p).length === 4) {
        console.log(`[เซิร์ฟเวอร์] สวน ${garden_id} มีครบ 4 จุดแล้ว! กำลังแจ้งหน้าเว็บ...`);
        broadcast({
            type: 'new_garden_added',
            data: { garden_id, coords: currentPoints, created_at: gardenData[garden_id].created_at }
        });
    }
    res.status(200).send({ message: 'Point received' });
});

// ===================================================================
//  !!! จุดแก้ไขสำคัญ: เพิ่ม Logic การคำนวณและแสดงผลค่าสีใน Grid !!!
// ===================================================================
app.post('/api/upload_color', (req, res) => {
    const { garden_id, latitude, longitude, r, g, b } = req.body;
    console.log(`[API] ได้รับข้อมูล Color จากสวน ${garden_id} ที่พิกัด (${latitude}, ${longitude})`);

    // ตรวจสอบว่ามีข้อมูลกรอบของสวนนี้หรือไม่
    if (!gardenData[garden_id] || gardenData[garden_id].coords.length !== 4) {
        console.warn(`[คำเตือน] ได้รับค่าสีของสวน ${garden_id} แต่ยังไม่มีข้อมูลกรอบครบ 4 จุด`);
        return res.status(400).send({ message: 'Garden boundary not defined yet' });
    }

    // คำนวณว่าพิกัดสีนี้ตกอยู่ในช่องกริดที่เท่าไหร่
    const cellIndex = calculateGridIndex(gardenData[garden_id].coords, { lat: latitude, lng: longitude });

    if (cellIndex !== -1) {
        console.log(`[คำนวณ] พิกัดสีตกอยู่ในช่องกริดที่: ${cellIndex + 1}`);
        // ส่งคำสั่งไปอัปเดตสีในตารางกริดบนหน้าเว็บ
        broadcast({
            type: 'update_grid_cell',
            data: {
                garden_id: garden_id,
                cell_index: cellIndex,
                color: { r, g, b }
            }
        });
    } else {
        console.log('[คำนวณ] พิกัดสีอยู่นอกกรอบของสวนนี้');
    }

    res.status(200).send({ message: 'Color received and processed' });
});

/**
 * คำนวณว่าจุดที่กำหนด (targetPoint) ตกอยู่ในช่องกริด (10x10) ที่เท่าไหร่ของ Polygon
 * @param {Array<Object>} polygonCoords - พิกัดมุม 4 จุดของ Polygon
 * @param {Object} targetPoint - พิกัดของค่าสีที่วัดได้ {lat, lng}
 * @returns {number} - Index ของช่องกริด (0-99) หรือ -1 ถ้าอยู่นอกกรอบ
 */
function calculateGridIndex(polygonCoords, targetPoint) {
    // หาขอบเขตของ Polygon
    const minLat = Math.min(...polygonCoords.map(p => p.lat));
    const maxLat = Math.max(...polygonCoords.map(p => p.lat));
    const minLng = Math.min(...polygonCoords.map(p => p.lng));
    const maxLng = Math.max(...polygonCoords.map(p => p.lng));

    // ตรวจสอบเบื้องต้นว่าจุดอยู่นอกกรอบสี่เหลี่ยมหรือไม่
    if (targetPoint.lat < minLat || targetPoint.lat > maxLat || targetPoint.lng < minLng || targetPoint.lng > maxLng) {
        return -1;
    }

    // คำนวณตำแหน่งสัมพัทธ์ของจุดในกรอบ (ค่าจะอยู่ระหว่าง 0.0 ถึง 1.0)
    const relativeLat = (targetPoint.lat - minLat) / (maxLat - minLat);
    const relativeLng = (targetPoint.lng - minLng) / (maxLng - minLng);

    // แปลงตำแหน่งสัมพัทธ์เป็นตำแหน่งในกริด 10x10
    // สมมติว่า Lat คือแกน Y (แถว) และ Lng คือแกน X (คอลัมน์)
    // ต้องกลับค่า Lat เพราะแผนที่ปกติแกน Y เพิ่มจากล่างขึ้นบน
    const row = Math.floor((1 - relativeLat) * 10);
    const col = Math.floor(relativeLng * 10);
    
    // แปลง (แถว, คอลัมน์) เป็น index (0-99)
    const index = (row * 10) + col;

    // ตรวจสอบให้แน่ใจว่า index อยู่ในขอบเขต 0-99
    return (index >= 0 && index < 100) ? index : -1;
}

// --- API: ลบสวน ---
app.delete('/api/delete_garden/:id', (req, res) => {
    const gardenId = req.params.id;
    if (gardenData[gardenId]) {
        delete gardenData[gardenId];
        broadcast({ type: 'garden_deleted', data: { garden_id: gardenId } });
        res.status(200).send({ message: 'Garden deleted' });
    } else {
        res.status(404).send({ message: 'Garden not found' });
    }
});

// --- WebSocket Server ---
wss.on('connection', ws => console.log('[WebSocket] มีหน้าเว็บใหม่เชื่อมต่อเข้ามา'));
function broadcast(message) {
    const jsonMessage = JSON.stringify(message);
    wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) client.send(jsonMessage);
    });
}

// --- เริ่มการทำงานของเซิร์ฟเวอร์ ---
const PORT = 3000;
server.listen(PORT, () => console.log(`[เซิร์ฟเวอร์] เริ่มทำงานที่ http://localhost:${PORT}` ));
