import os
import json
import requests

# ====== API URLs ======
API_COLOR_URL = "http://localhost:3000/api/upload_color"
API_POINT_URL = "http://localhost:3000/api/upload_point"


# ====== Upload Functions ======
def upload_color(data):
    try:
        response = requests.post(API_COLOR_URL, json=data, timeout=5)
        if response.status_code == 200:
            print(f"✅ Uploaded color: garden {data['garden_id']}, cell @ ({data['latitude']},{data['longitude']})")
        else:
            print(f"❌ Failed color upload: {response.status_code}, {response.text}")
    except Exception as e:
        print(f"⚠️ Error uploading color: {e}")


def upload_point(data):
    try:
        response = requests.post(API_POINT_URL, json=data, timeout=5)
        if response.status_code == 200:
            print(f"✅ Uploaded point: garden {data['garden_id']} | point {data['point_no']}")
        else:
            print(f"❌ Failed point upload: {response.status_code}, {response.text}")
    except Exception as e:
        print(f"⚠️ Error uploading point: {e}")


# ====== Main Process ======
def main():
    # หาตำแหน่งไฟล์แบบอัตโนมัติ (ไม่ต้องสนใจว่าจะรันจากไหน)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    file_path = os.path.join(script_dir, "datatest.json")

    # โหลดข้อมูลจาก datatest.json
    if not os.path.exists(file_path):
        print(f"❌ ไม่พบไฟล์ {file_path}")
        return

    with open(file_path, "r", encoding="utf-8") as f:
        testdata = json.load(f)

    # ===== Upload Points =====
    print("\n=== Uploading Points ===")
    for point in testdata.get("points", []):
        upload_point(point)

    # ===== Upload Colors =====
    print("\n=== Uploading Colors ===")
    for color in testdata.get("colors", []):
        upload_color(color)


# ====== Entry Point ======
if __name__ == "__main__":
    main()
