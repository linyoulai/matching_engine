import requests
import time
import concurrent.futures
from requests.adapters import HTTPAdapter  # 直接导入解决红线问题

# 网关地址
BASE_URL = "http://localhost:8080"

# 使用 Session 实现长连接复用，防止 Errno 99 (端口耗尽)
session = requests.Session()
adapter = HTTPAdapter(
    pool_connections=100, 
    pool_maxsize=100
)
session.mount('http://', adapter)

def test_submit_order(trader_id, symbol_id):
    url = f"{BASE_URL}/submit_order"
    payload = {
        "symbol_id": symbol_id,
        "trader_id": trader_id,
        "price": 10050,
        "qty": 200,
        "side": "BUY",
        "order_type": "LIMIT",
        "tif": "GTC"
    }
    try:
        start_time = time.time()
        # 使用 session 而不是 requests 直接发送
        response = session.post(url, json=payload, timeout=5)
        latency = (time.time() - start_time) * 1000
        
        if response.status_code == 200:
            # 这里的打印会比较多，高并发下建议只打印错误或统计数据
            pass 
        else:
            print(f"[FAILED] Trader {trader_id} | Status: {response.status_code}")
    except Exception as e:
        print(f"[ERROR] Trader {trader_id} | {e}")

if __name__ == "__main__":
    print("--- Starting High-Concurrency Session Test ---")
    
    total_requests = 1000
    max_workers = 50 # 适当增加并发线程数
    
    start_all = time.time()
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        # 提交 1000 个下单请求
        futures = [executor.submit(test_submit_order, 1000 + i, 888) for i in range(total_requests)]
        concurrent.futures.wait(futures)
    
    end_all = time.time()
    total_time = end_all - start_all
    
    print("\n--- Test Finished ---")
    print(f"Total Requests: {total_requests}")
    print(f"Total Time: {total_time:.2f} seconds")
    print(f"Throughput: {total_requests / total_time:.2f} req/s")
    # 输出平均每个请求的平均延迟
    print(f"Average Latency: {total_time / total_requests * 1000:.2f} ms")