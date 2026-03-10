import requests
import pytest
import time

BASE_URL = "http://localhost:8080"
HEADERS = {'Content-Type': 'application/json'}

def submit_order(payload):
    res = requests.post(f"{BASE_URL}/submit_order", json=payload, headers=HEADERS)
    assert res.status_code == 200, f"Order failed: {res.text}"
    return res.json()

def cancel_order(order_id, symbol_id, trader_id):
    payload = {"order_id": order_id, "symbol_id": symbol_id, "trader_id": trader_id}
    res = requests.post(f"{BASE_URL}/cancel_order", json=payload, headers=HEADERS)
    return res

def query_order(order_id, symbol_id, trader_id):
    payload = {"order_id": order_id, "symbol_id": symbol_id, "trader_id": trader_id}
    res = requests.post(f"{BASE_URL}/query_order", json=payload, headers=HEADERS)
    return res

def wait_match():
    time.sleep(0.2)

def reset_engine():
    # 若有重置接口可调用，否则重启引擎
    pass

# def test_limit_full_match():
#     """限价单完全成交"""
#     buy = {"symbol_id": 1, "trader_id": 1, "price": 10000, "qty": 100, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
#     sell = {"symbol_id": 1, "trader_id": 2, "price": 10000, "qty": 100, "side": "SELL", "order_type": "LIMIT", "tif": "GTC"}
#     buy_res = submit_order(buy)
#     sell_res = submit_order(sell)
#     wait_match()
#     assert buy_res["order_id"] != 0 and sell_res["order_id"] != 0

# def test_limit_partial_match():
#     """限价单部分成交"""
#     buy = {"symbol_id": 2, "trader_id": 1, "price": 10000, "qty": 200, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
#     sell = {"symbol_id": 2, "trader_id": 2, "price": 10000, "qty": 100, "side": "SELL", "order_type": "LIMIT", "tif": "GTC"}
#     buy_res = submit_order(buy)
#     sell_res = submit_order(sell)
#     wait_match()
#     # 买单剩余100未成交
#     q = query_order(buy_res["order_id"], 2, 1)
#     assert q.status_code == 200 and q.json().get("filled_qty", 0) == 100

# def test_limit_unmatch():
#     """限价单无法成交"""
#     buy = {"symbol_id": 3, "trader_id": 1, "price": 9900, "qty": 100, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
#     sell = {"symbol_id": 3, "trader_id": 2, "price": 10100, "qty": 100, "side": "SELL", "order_type": "LIMIT", "tif": "GTC"}
#     buy_res = submit_order(buy)
#     sell_res = submit_order(sell)
#     wait_match()
#     # 均未成交
#     q = query_order(buy_res["order_id"], 3, 1)
#     assert q.status_code == 200 and q.json().get("filled_qty", 0) == 0

def test_cancel_order():
    """撤单测试"""
    # buy = {"symbol_id": 4, "trader_id": 1, "price": 10000, "qty": 100, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
    # buy_res = submit_order(buy)
    # wait_match()
    res = cancel_order("14872750390100721665", 4, 1)
    assert res.status_code == 200 and res.json().get("status") in ("CANCELED", "REJECTED")

# def test_cancel_filled_order():
#     """已成交订单撤单应被拒绝"""
#     buy = {"symbol_id": 5, "trader_id": 1, "price": 10000, "qty": 100, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
#     sell = {"symbol_id": 5, "trader_id": 2, "price": 10000, "qty": 100, "side": "SELL", "order_type": "LIMIT", "tif": "GTC"}
#     buy_res = submit_order(buy)
#     sell_res = submit_order(sell)
#     wait_match()
#     res = cancel_order(buy_res["order_id"], 5, 1)
#     assert res.status_code == 200 and res.json().get("status") == "REJECTED"

# def test_market_order():
#     """市价单撮合"""
#     buy = {"symbol_id": 6, "trader_id": 1, "price": 0, "qty": 100, "side": "BUY", "order_type": "MARKET", "tif": "IOC"}
#     sell = {"symbol_id": 6, "trader_id": 2, "price": 10000, "qty": 100, "side": "SELL", "order_type": "LIMIT", "tif": "GTC"}
#     sell_res = submit_order(sell)
#     buy_res = submit_order(buy)
#     wait_match()
#     assert buy_res["order_id"] != 0

# def test_price_limit():
#     """涨跌停保护"""
#     buy = {"symbol_id": 7, "trader_id": 1, "price": 20000, "qty": 100, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
#     res = requests.post(f"{BASE_URL}/submit_order", json=buy, headers=HEADERS)
#     assert res.status_code == 200 and res.json().get("status") == "REJECTED"

# def test_self_trade():
#     """自成交保护"""
#     buy = {"symbol_id": 8, "trader_id": 1, "price": 10000, "qty": 100, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
#     sell = {"symbol_id": 8, "trader_id": 1, "price": 10000, "qty": 100, "side": "SELL", "order_type": "LIMIT", "tif": "GTC"}
#     buy_res = submit_order(buy)
#     sell_res = submit_order(sell)
#     wait_match()
#     # 订单应被拒绝或部分成交
#     q = query_order(buy_res["order_id"], 8, 1)
#     assert q.status_code == 200

# def test_invalid_param():
#     """非法参数"""
#     buy = {"symbol_id": 9, "trader_id": 1, "price": -1, "qty": 100, "side": "BUY", "order_type": "LIMIT", "tif": "GTC"}
#     res = requests.post(f"{BASE_URL}/submit_order", json=buy, headers=HEADERS)
#     assert res.status_code != 200 or res.json().get("status") == "REJECTED"