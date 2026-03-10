这是一个接近真实交易所的撮合引擎项目。  

特别说明：

1. 当前默认按单标的运行（要做多标的，建议多实例）。
2. 不管理用户资金和持仓，这部分应该由券商柜台/风控/清结算系统负责。

---

## 这个项目到底在做什么

一句话：
用户发 HTTP 下单/撤单/查单 -> 网关解析 JSON -> 入无锁队列 -> 撮合引擎消费并撮合 -> 回报队列异步返回。

你可以把它理解成一个简化但完整的交易链路：

1. 有网关（Gateway）
2. 有订单簿（OrderBook）
3. 有撮合引擎（MatchingEngine）
4. 有异步队列解耦
5. 有可重复压测入口

---

## 模块设计（按链路顺序）

### 1) Gateway（网关）

网关负责两件事：

1. 对外提供 HTTP 接口（submit/cancel/query）
2. 对内把请求转成 RequestEnvelope 投递到请求队列

当前接口：

1. `POST /submit_order`
2. `POST /cancel_order`
3. `POST /query_order`
4. `POST /subscribe_market_data`（占位，后续可扩展）

为什么要有网关：

1. 隔离协议层（HTTP/JSON）和撮合核心
2. 撮合引擎只关心 Order/Cancel，不关心网络细节
3. 后续替换成 WebSocket / gRPC，核心撮合代码基本不用动

### 2) Request/Response（请求与回报）

请求分三类：

1. 下单
2. 撤单
3. 查单

成交回报通过异步队列返回，网关再消费并维护订单状态映射，供查单接口读取。

### 3) OrderBook（订单簿）

当前实现是 `vector + list + unordered_map`：

1. `vector` 按价格档位存队列（价格索引连续，CPU 友好）
2. `list` 维护同价位时间优先（FIFO）
3. `unordered_map<order_id, iterator>` 支撑 O(1) 级别查找撤单

设计直觉：
A 股日内价格区间有限，按价位开 `vector` 比树结构更快，缓存命中更好。

### 4) MatchingEngine（单线程撮合）

撮合引擎是核心消费者：

1. 从请求队列拉取请求
2. 下单则尝试撮合，不可成交部分入簿
3. 撤单则在 order_map 中定位后删除
4. 生成成交回报，推送到回报队列

支持的订单语义：

1. `OrderType`: LIMIT / MARKET
2. `TimeInForce`: GTC / IOC / FOK

已做的风控约束：

1. 价格上下限检查（超涨跌停拒绝）
2. 市价单 5 档保护（防止大额扫穿）
3. 自成交拦截（匹配到同 trader_id 时终止吃单）

---

## 关键数据结构

### Order 字段

1. `order_id`（当前用网关 tag）
2. `ts`
3. `symbol_id`
4. `trader_id`
5. `price`
6. `qty`
7. `filled_qty`
8. `side`
9. `order_type`
10. `tif`
11. `order_status`

### tag 生成方式

当前是：`timestamp + symbol_id + sequence` 拼接。  
优点是快、局部有序、实现简单。  
注意点是：时钟回拨和跨核可见性问题要明确处理策略。

---

## 接口示例

### 1) 提交订单

```
POST /submit_order
```

请求：

```json
{
  "symbol_id": 888,
  "trader_id": 1001,
  "price": 10050,
  "qty": 200,
  "side": "BUY",
  "order_type": "LIMIT",
  "tif": "GTC"
}
```

响应：

```json
{
  "status": "ACCEPTED",
  "order_id": 1741416550000001,
  "timestamp": 1741416550
}
```

### 2) 撤单

```
POST /cancel_order
```

请求：

```json
{
  "order_id": 1741416550000001,
  "symbol_id": 888,
  "trader_id": 1001
}
```

响应（示例）：

```json
{
  "status": "ACCEPTED"
}
```

### 3) 查单

```
POST /query_order
```

请求：

```json
{
  "order_id": 1741416550000001,
  "symbol_id": 888,
  "trader_id": 1001
}
```

响应（示例）：

```json
{
  "order_id": 1741416550000001,
  "status": "PARTIAL_FILLED",
  "message": "Success"
}
```

---

## 如何运行

### 构建

```bash
cmake -S . -B build
cmake --build build -j
```

### 普通模式

```bash
./build/main
```

### 队列链路压测（函数直调）

```bash
./build/main --stress 16 1000000
```

### HTTP 链路压测（真实走 JSON 解析）

```bash
./build/main --http-stress 16 2000
```

参数说明：

1. 第一个参数：并发线程数
2. 第二个参数：每线程操作次数

---

## 压测结果（windows11 16个逻辑处理器）

### 1) 请求队列 -> 撮合引擎 -> 回报队列

量级大约 `4.3M ~ 4.7M TPS`（submit+cancel 合计）。

样例：

1. `--stress 16 200000` -> `elapsed_ms=1345`
2. `--stress 16 500000` -> `elapsed_ms=3681`
3. `--stress 16 1000000` -> `elapsed_ms=7424`

### 2) HTTP JSON 解析 -> 请求队列 -> 撮合引擎 -> 回报队列

当前大约 `380~400 QPS`。  
这说明瓶颈主要在 HTTP/JSON 层，而不是撮合核心。

样例：

1. `--http-stress 16 20` -> `total_qps≈372`
2. `--http-stress 16 200` -> `total_qps≈369`
3. `--http-stress 16 2000` -> `total_qps≈399`

---

## 设计思考（这部分很重要）

做系统设计时，我习惯问四个问题：

1. 是什么？
2. 有什么用？
3. 有什么优点？
4. 有什么隐患？

下面是项目里的几个典型例子。

### Q1: 原子自增 ID 和 UUID 有什么区别？

1. 原子自增：快、天然有序、cache 友好；但是单点序列，要考虑跨实例策略。
2. UUID：全局唯一更方便分布式；但空间大、无序、索引局部性差。

### Q2: 直接用纳秒时间戳就行吗？

不行。  
高并发下同一时钟粒度可能重复，跨核取时也可能碰撞。

### Q3: 时钟回拨怎么办？

1. 交易排序最好不要依赖系统时间。
2. 可用单调时钟（steady_clock）做相对时间。
3. 更稳妥的是用引擎内递增序列作为最终时序依据。

### Q4: 行情快照和撮合线程会不会锁竞争？

会。  
如果读写模型不当，会出现写者/读者饥饿，吞吐抖动。

### Q5: shared_mutex 会不会让 unique_lock？

标准不保证公平策略。某些实现上确实可能让写者饥饿。  
所以高频场景要评估替代方案（比如 seqlock/RCU/快照复制）。

### Q6: 原子变量是不是“没有成本”？

不是。  
原子带来的开销核心在缓存一致性协议（MESI）引发的跨核同步，不是“完全无开销”。

---

## 当前不足和下一步

现在这个工程已经能做：

1. 功能闭环
2. 并发压测

但要继续往“实盘级别”靠，还要补：

1. 多标的分片与隔离
2. 回报/行情 WebSocket 推送
3. 落盘与恢复
4. 更细粒度性能指标（分阶段耗时、尾延迟）
5. 完整自动化测试与故障注入