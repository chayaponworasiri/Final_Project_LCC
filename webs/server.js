// =====================================================
// server.js (SQLite + WebSocket + Grid Color + Lat/Lng)
// =====================================================
const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const path = require('path');
const sqlite3 = require('sqlite3').verbose();

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

app.use(express.json());

// --- SQLite ---
const db = new sqlite3.Database('./database/garden.db', (err) => {
    if (err) console.error('ไม่สามารถเชื่อมต่อ Database ได้:', err.message);
    else console.log('เชื่อมต่อ SQLite สำเร็จ');
});

// --- ตรวจสอบ/สร้างตาราง ---
db.serialize(() => {
    db.run(`CREATE TABLE IF NOT EXISTS gardens (
        garden_id TEXT PRIMARY KEY,
        created_at TEXT
    )`);

    db.run(`CREATE TABLE IF NOT EXISTS garden_points (
        garden_id TEXT,
        point_no INTEGER,
        lat REAL,
        lng REAL,
        PRIMARY KEY (garden_id, point_no),
        FOREIGN KEY (garden_id) REFERENCES gardens(garden_id)
    )`);

    db.run(`CREATE TABLE IF NOT EXISTS garden_colors (
        garden_id TEXT,
        cell_index INTEGER,
        r INTEGER,
        g INTEGER,
        b INTEGER,
        lat REAL,
        lng REAL,
        timestamp TEXT,
        FOREIGN KEY (garden_id) REFERENCES gardens(garden_id)
    )`);

    // ตรวจสอบว่ามีคอลัมน์ lat/lng หรือไม่
    db.all(`PRAGMA table_info(garden_colors);`, (err, cols) => {
        if (!cols.some(c => c.name === 'lat')) db.run(`ALTER TABLE garden_colors ADD COLUMN lat REAL`);
        if (!cols.some(c => c.name === 'lng')) db.run(`ALTER TABLE garden_colors ADD COLUMN lng REAL`);
    });
});

// --- Memory cache ---
const gardenData = {}; // { garden_id: { coords: [], created_at, grid: [ {r,g,b,lat,lng} ] } }

// --- Load from DB ---
function loadGardensFromDB() {
    return new Promise((resolve, reject) => {
        db.all(`SELECT * FROM gardens`, [], (err, gardens) => {
            if (err) return reject(err);
            gardens.forEach(g => {
                gardenData[g.garden_id] = {
                    coords: [],
                    created_at: g.created_at,
                    grid: Array.from({ length: 100 }, () => ({ r: 0, g: 0, b: 0, lat: null, lng: null }))
                };
            });

            db.all(`SELECT * FROM garden_points`, [], (err, points) => {
                if (err) return reject(err);
                points.forEach(p => {
                    if (gardenData[p.garden_id]) {
                        gardenData[p.garden_id].coords[p.point_no - 1] = { lat: p.lat, lng: p.lng };
                    }
                });

                db.all(`SELECT * FROM garden_colors`, [], (err, colors) => {
                    if (err) return reject(err);
                    colors.forEach(c => {
                        if (gardenData[c.garden_id]) {
                            gardenData[c.garden_id].grid[c.cell_index] = { r: c.r, g: c.g, b: c.b, lat: c.lat, lng: c.lng };
                        }
                    });

                    console.log('[DB] โหลดสวน, จุด, สีครบทุกช่อง');
                    resolve();
                });
            });
        });
    });
}

// --- Endpoints ---
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'index.html')));

app.get('/api/get_all_gardens', (req, res) => {
    const allGardens = Object.keys(gardenData).map(id => ({
        garden_id: id,
        coords: gardenData[id].coords,
        created_at: gardenData[id].created_at,
        grid: gardenData[id].grid
    }));
    res.json(allGardens);
});

app.get('/api/get_garden_colors/:gardenId', (req,res)=>{
    const gardenId = req.params.gardenId;
    db.all(`SELECT * FROM garden_colors WHERE garden_id=?`, [gardenId], (err, rows)=>{
        if(err) return res.status(500).json({error:err.message});
        res.json(rows);
    });
});

app.post('/api/upload_point', (req, res) => {
    const { garden_id, point_no, latitude, longitude } = req.body;
    if (!gardenData[garden_id]) {
        gardenData[garden_id] = { coords: [], created_at: new Date().toISOString(), grid: Array.from({ length: 100 }, () => ({ r:0,g:0,b:0,lat:null,lng:null })) };
        db.run(`INSERT OR IGNORE INTO gardens(garden_id, created_at) VALUES(?, ?)`, [garden_id, gardenData[garden_id].created_at]);
    }
    gardenData[garden_id].coords[point_no - 1] = { lat: latitude, lng: longitude };
    db.run(`INSERT OR REPLACE INTO garden_points(garden_id, point_no, lat, lng) VALUES (?, ?, ?, ?)`, [garden_id, point_no, latitude, longitude]);

    if (gardenData[garden_id].coords.filter(p => p).length === 4) {
        broadcast({ type: 'new_garden_added', data: { garden_id, coords: gardenData[garden_id].coords, created_at: gardenData[garden_id].created_at } });
    }

    res.json({ message: 'Point received' });
});

app.post('/api/upload_color', (req, res) => {
    const { garden_id, latitude, longitude, r, g, b } = req.body;
    if (!gardenData[garden_id] || gardenData[garden_id].coords.length !== 4) return res.status(400).json({ message: 'Garden boundary not defined yet' });

    const cellIndex = calculateGridIndex(gardenData[garden_id].coords, { lat: latitude, lng: longitude });
    if (cellIndex !== -1) {
        gardenData[garden_id].grid[cellIndex] = { r, g, b, lat: latitude, lng: longitude };
        db.run(`INSERT INTO garden_colors(garden_id, cell_index, r, g, b, lat, lng, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
            [garden_id, cellIndex, r, g, b, latitude, longitude, new Date().toISOString()]);

        broadcast({ type: 'update_grid_cell', data: { garden_id, cell_index, color: { r, g, b, lat: latitude, lng: longitude } } });
    }

    res.json({ message: 'Color received' });
});

app.delete('/api/delete_garden/:id', (req, res) => {
    const gardenId = req.params.id;
    if (gardenData[gardenId]) {
        delete gardenData[gardenId];
        db.run(`DELETE FROM gardens WHERE garden_id=?`, [gardenId]);
        db.run(`DELETE FROM garden_points WHERE garden_id=?`, [gardenId]);
        db.run(`DELETE FROM garden_colors WHERE garden_id=?`, [gardenId]);
        broadcast({ type: 'garden_deleted', data: { garden_id: gardenId } });
        return res.json({ message: 'Garden deleted' });
    }
    res.status(404).json({ message: 'Garden not found' });
});

// --- WebSocket ---
function broadcast(message) {
    const jsonMessage = JSON.stringify(message);
    wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) client.send(jsonMessage);
    });
}

// --- Grid Calculation ---
function calculateGridIndex(coords, point) {
    const minLat = Math.min(...coords.map(p => p.lat));
    const maxLat = Math.max(...coords.map(p => p.lat));
    const minLng = Math.min(...coords.map(p => p.lng));
    const maxLng = Math.max(...coords.map(p => p.lng));

    if (point.lat < minLat || point.lat > maxLat || point.lng < minLng || point.lng > maxLng) return -1;

    const relLat = (point.lat - minLat) / (maxLat - minLat);
    const relLng = (point.lng - minLng) / (maxLng - minLng);

    const row = Math.floor((1 - relLat) * 10);
    const col = Math.floor(relLng * 10);

    const index = row * 10 + col;
    return index >= 0 && index < 100 ? index : -1;
}

// --- Start Server ---
loadGardensFromDB().then(() => {
    const PORT = 3000;
    server.listen(PORT, () => console.log(`[Server] Running at http://localhost:${PORT}`));
});
