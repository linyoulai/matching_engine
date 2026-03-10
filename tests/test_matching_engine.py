import pytest
import subprocess
import time
import os

# 假设有 main 可执行文件和 HTTP 或本地接口，或通过 test_gateway.py 驱动
# 这里只做功能测试框架示例，具体断言和数据需结合实际接口实现

BIN_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), '../build/main'))

@pytest.fixture(scope="module", autouse=True)
def start_engine():
    # 启动撮合引擎进程
    proc = subprocess.Popen([BIN_PATH])
    time.sleep(1)  # 等待引擎启动
    yield
    proc.terminate()
    proc.wait()

# 示例：通过 test_gateway.py 驱动接口测试
@pytest.mark.parametrize("order_case", [
    "normal_match", "partial_fill", "cancel_order", "market_order", "ioc_order", "fok_order", "price_limit", "self_trade"
])
def test_matching_cases(order_case):
    # 假设 test_gateway.py 支持不同case参数
    result = subprocess.run([
        "python3", "test_gateway.py", order_case
    ], cwd=os.path.dirname(__file__), capture_output=True, text=True)
    assert result.returncode == 0, f"Case {order_case} failed: {result.stderr}"
    # 可根据输出内容进一步断言
    assert "PASS" in result.stdout or "success" in result.stdout.lower()
