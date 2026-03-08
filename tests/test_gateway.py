import requests
import json
import time

BASE_URL = "http://localhost:8080"
HEADERS = {'Content-Type': 'application/json'}

def print_result(name, res):
    if res.status_code == 200:
        print(f"[PASS] {name}: {res.json()}")
        return res.json().get('order_id') # 提取 OrderID 用于后续测试
    else:
        print(f"[FAIL] {name}: Status {res.status_code}, Body {res.text}")
        return None

# ================= 1. 正常下单测试 =================
print("--- 1. Submit Order Test ---")
payload_limit = {
    "symbol_id": 888, 
    "trader_id": 1001, 
    "price": 10050, 
    "qty": 200, 
    "side": "BUY", 
    "order_type": 
    "LIMIT", 
    "tif": "GTC"
}
order_id = print_result("Submit Valid Limit Order", 
                        requests.post(f"{BASE_URL}/submit_order", json=payload_limit, headers=HEADERS))

# ================= 2. 异常输入测试 =================
print("\n--- 2. Invalid Input Test ---")
# 缺少 price
payload_invalid = {"symbol_id": 888, "trader_id": 1001, "qty": 200}
requests.post(f"{BASE_URL}/submit_order", json=payload_invalid, headers=HEADERS)
# 期待: 你的 C++ 控制台应该打印 "JSON parse error" 或类似日志，且返回 400

# ================= 3. 查单测试 (验证 NEW 状态) =================
print("\n--- 3. Query Order Test (NEW) ---")
if order_id:
    query_payload = {"order_id": order_id, "symbol_id": 888, "trader_id": 1001}
    res = requests.post(f"{BASE_URL}/query_order", json=query_payload, headers=HEADERS)
    data = res.json()
    if data.get('status') == 'NEW':
        print(f"[PASS] Order Status is NEW")
    else:
        print(f"[FAIL] Expected NEW, got {data.get('status')}")

# ================= 4. 查不存在的单 =================
print("\n--- 4. Query Non-existent Order ---")
bad_query = {"order_id": 99999999999, "symbol_id": 888, "trader_id": 1001}
res = requests.post(f"{BASE_URL}/query_order", json=bad_query, headers=HEADERS)
if res.status_code == 404: # 你代码里设置了 res.status = 404
    print("[PASS] Correctly returned 404 for missing order")
else:
    print(f"[FAIL] Expected 404, got {res.status_code}")