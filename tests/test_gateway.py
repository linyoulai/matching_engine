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

# 下买单
print("--- 1. Submit Order Test ---")
payload_limit = {
    "symbol_id": 888, 
    "trader_id": 1001, 
    "price": 10050, 
    "qty": 200, 
    "side": "BUY", 
    "order_type": "LIMIT", 
    "tif": "GTC"
}
order_id = print_result("Submit Valid Limit Order", 
                        requests.post(f"{BASE_URL}/submit_order", json=payload_limit, headers=HEADERS))

# # 下个无法撮合的卖单
# payload_sell = {
#     "symbol_id": 888, 
#     "trader_id": 1002, 
#     "price": 10100, 
#     "qty": 100, 
#     "side": "SELL", 
#     "order_type": "LIMIT", 
#     "tif": "GTC"
# }
# print_result("Submit Unmatchable Sell Order", 
#              requests.post(f"{BASE_URL}/submit_order", json=payload_sell, headers=HEADERS))

# # 下个可撮合的卖单
# payload_sell_match = {
#     "symbol_id": 888, 
#     "trader_id": 1003, 
#     "price": 10050, 
#     "qty": 150, 
#     "side": "SELL", 
#     "order_type": "LIMIT", 
#     "tif": "GTC"
# }
# print_result("Submit Matchable Sell Order", 
#              requests.post(f"{BASE_URL}/submit_order", json=payload_sell_match, headers=HEADERS))

# 下个卖单把对方全都吃掉
payload_sell_full = {
    "symbol_id": 888, 
    "trader_id": 1004, 
    "price": 10000, 
    "qty": 300, 
    "side": "SELL", 
    "order_type": "LIMIT", 
    "tif": "GTC"
}
print_result("Submit Fully Matchable Sell Order", 
             requests.post(f"{BASE_URL}/submit_order", json=payload_sell_full, headers=HEADERS))