import json
import random

# ===== 4 Points ของ Garden 1 =====
points = [
    {"garden_id":1, "point_no":1, "latitude":14.045, "longitude":100.610},
    {"garden_id":1, "point_no":2, "latitude":14.045, "longitude":100.615},
    {"garden_id":1, "point_no":3, "latitude":14.042, "longitude":100.615},
    {"garden_id":1, "point_no":4, "latitude":14.042, "longitude":100.610},
]

# ===== สร้างสีใน grid 10x10 =====
colors = []
lat_min, lat_max = 14.042, 14.045
lng_min, lng_max = 100.610, 100.615

rows, cols = 10, 10

for i in range(rows):
    for j in range(cols):
        lat = lat_max - i*(lat_max-lat_min)/rows
        lng = lng_min + j*(lng_max-lng_min)/cols
        color = {
            "device_id":"esp32s3_01",
            "garden_id":1,
            "latitude":round(lat,6),
            "longitude":round(lng,6),
            "r": random.randint(150,255),
            "g": random.randint(150,255),
            "b": random.randint(0,100),
            "ts": random.randint(1000000,2000000)
        }
        colors.append(color)

# ===== รวม data =====
data = {
    "points": points,
    "colors": colors
}

# ===== เขียนลงไฟล์ datatest.json =====
with open("datatest.json","w") as f:
    json.dump(data,f,indent=2)

print("datatest.json created with garden 1, 4 points and 100 colors")
