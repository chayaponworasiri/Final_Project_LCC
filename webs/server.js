// File: server.js (เวอร์ชัน Final - จัดการข้อมูลแบบไดนามิก)

const express = require('express');
const http = require('http' );
const WebSocket = require('ws');
const path = require('path');
const Database = require('better-sqlite3');

const app = express();
const server = http.createServer(app );
const wss = new WebSocket.Server({ server });

app.use(express.json({ limit: '5mb' }));

// --- Database Setup ---
const db = new Database('smart_farm.db', { verbose: console.log });
db.exec(`
    CREATE TABLE IF NOT EXISTS gardens (
        garden_id INTEGER PRIMARY KEY,
        device_id TEXT NOT NULL,
        points_json TEXT,
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS color_readings (
        reading_id INTEGER PRIMARY KEY AUTOINCREMENT,
        garden_id INTEGER NOT NULL,
        device_id TEXT NOT NULL,
        latitude REAL NOT NULL, longitude REAL NOT NULL,
        r INTEGER, g INTEGER, b INTEGER,
        measured_at DATETIME,
        FOREIGN KEY (garden_id) REFERENCES gardens (garden_id)
    );
`);

// --- Endpoint หลัก: ส่งไฟล์ index.html ---
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// --- API สำหรับให้หน้าเว็บดึงข้อมูลสวนทั้งหมดเมื่อเปิดครั้งแรก ---
app.get('/api/get_all_gardens', (req, res) => {
    try {
        const stmt = db.prepare('SELECT garden_id, points_json, created_at FROM gardens ORDER BY garden_id');
        const gardens = stmt.all();
        // แปลง points_json กลับเป็น Object ก่อนส่ง
        const formattedGardens = gardens.map(g => ({
            garden_id: g.garden_id,
            coords: JSON.parse(g.points_json),
            created_at: g.created_at
        }));
        res.status(200).json(formattedGardens);
    } catch (error) {
        console.error('[DB Error] Cannot get gardens:', error);
        res.status(500).json({ message: 'Failed to retrieve gardens' });
    }
});


// --- API สำหรับรับข้อมูล Sync จาก ESP32 ---
app.post('/api/sync', (req, res) => {
    const { device_id, points } = req.body; // สนใจแค่ points ตอนนี้
    console.log(`[API SYNC] ได้รับข้อมูลจาก: ${device_id}`);

    if (!device_id || !points || !points.garden_id || points.coords.length !== 4) {
        return res.status(400).send({ message: 'Invalid sync data' });
    }

    try {
        const { garden_id, coords } = points;
        const stmt = db.prepare('INSERT OR REPLACE INTO gardens (garden_id, device_id, points_json) VALUES (?, ?, ?)');
        const info = stmt.run(garden_id, device_id, JSON.stringify(coords));
        
        console.log(`[DB] บันทึกสวน ID: ${garden_id} สำเร็จ`);

        // ส่งข้อมูลสวนใหม่นี้ไปให้หน้าเว็บทุกหน้าทันที
        broadcast({
            type: 'new_garden_added',
            data: {
                garden_id: garden_id,
                coords: coords,
                created_at: new Date().toISOString() // ส่งเวลาปัจจุบันไปก่อน
            }
        });

        res.status(200).send({ message: 'Sync successful' });
    } catch (error) {
        console.error('[DB Error] Sync failed:', error);
        res.status(500).send({ message: 'Server error during sync' });
    }
});

// ======================================================
//  API ใหม่: สำหรับลบข้อมูลสวน
// ======================================================
app.delete('/api/delete_garden/:id', (req, res) => {
    const gardenId = req.params.id;
    console.log(`[API DELETE] ได้รับคำขอลบสวน ID: ${gardenId}`);
    try {
        // ใช้ transaction เพื่อความปลอดภัย ลบทั้ง 2 ตาราง
        const deleteGarden = db.transaction(() => {
            // 1. ลบข้อมูลค่าสีที่เกี่ยวข้องกับสวนนี้ก่อน
            const colorStmt = db.prepare('DELETE FROM color_readings WHERE garden_id = ?');
            colorStmt.run(gardenId);
            // 2. ลบข้อมูลสวนหลัก
            const gardenStmt = db.prepare('DELETE FROM gardens WHERE garden_id = ?');
            const info = gardenStmt.run(gardenId);
            return info;
        });

        const result = deleteGarden();

        if (result.changes > 0) {
            console.log(`[DB] ลบสวน ID: ${gardenId} สำเร็จ`);
            // แจ้งให้หน้าเว็บทุกหน้ารู้ว่าสวนนี้ถูกลบแล้ว
            broadcast({
                type: 'garden_deleted',
                data: { garden_id: gardenId }
            });
            res.status(200).send({ message: 'Garden deleted successfully' });
        } else {
            console.log(`[DB] ไม่พบสวน ID: ${gardenId} ให้ลบ`);
            res.status(404).send({ message: 'Garden not found' });
        }
    } catch (error) {
        console.error(`[DB Error] Failed to delete garden ${gardenId}:`, error);
        res.status(500).send({ message: 'Server error' });
    }
});


// --- WebSocket ---
wss.on('connection', ws => {
    console.log('[WebSocket] มีหน้าเว็บใหม่เชื่อมต่อเข้ามา');
});

function broadcast(message) {
    const jsonMessage = JSON.stringify(message);
    wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(jsonMessage);
        }
    });
}

// --- Start Server ---
const PORT = 3000;
server.listen(PORT, () => {
    console.log(`[เซิร์ฟเวอร์] เริ่มทำงานที่ http://localhost:${PORT}` );
});
