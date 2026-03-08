这是一个接近真实交易所的撮合引擎项目。

目的：帮助对金融市场感兴趣的小伙伴理解交易所撮合机制。

特别说明：
    1.此项目目前仅支持单个标的，如需支持多个标的，应该创建多个实例。
    2.不支持存储用户资金数据，用户资金数据应该由券商或结算中心管理。

模块包括：

1.订单（Order）：
    包含字段：order_id（直接用网关层打的tag），ts，symbol_id, trader_id, price，qty，filled_qty, side, order_type, tif, order_status
    订单类型枚举（OrderType）：包括限价单（LIMIT），市价单（MARKET）。
    TimeInForce：tif包括GTC，IOC，FOK。
    限价单合法tif: GTC，IOC，FOK
    市价单合法TIF: IOC，FOK
    订单状态（OrderStatus）：NEW，EXPIRED，PARTIAL_FILLED，FILLED，REJECTED。
    // 订单
    struct Order {};
    

2.请求(Request)：
    请求类型：下单，撤单，查单
    下单请求(OrderRequest)
    撤单请求
    查单请求

3.响应(Response):
    HTTP接口响应：
    成交回报：
    行情推送：


3.网关（Gateway）：开放HTTP接口。总共4个无锁队列，使用moodycamel::ConcurrentQueue。
    开放接口：
        HTTP接口：从接口中提取数据，打标签，放入队列
            /submit_order
                示例：
                    请求字段：
                    {
                        "symbol_id": 888,
                        "trader_id": 1001,
                        "price": 10050, 
                        "qty": 200,
                        "side": "BUY",
                        "order_type": "LIMIT",
                        "tif": "GTC"
                    }
                    响应：
                    {
                        "status": "ACCEPTED",
                        "order_id": 1741416550000001,
                        "message": "Order has been queued."
                    }
            /cancel_order
                示例：
                    {
                        "order_id": 1741416550000001,
                        "symbol_id": 888,
                        "trader_id": 1001
                    }
                    {
                        "status": "ACCEPTED",
                        "order_id": 1741416550000001,
                        "message": "Cancellation request queued."
                    }
                    {
                        "status": "SUCCESS",
                        "order_id": 1741416550000001,
                        "message": "canceled sucessfully"
                    }
                    {
                        "status": "Failed",
                        "order_id": 1741416550000001,
                        "message": "cancel failed"
                    }
            /query_order
                示例：
                    {
                        "order_id": 1741416550000001,
                        "symbol_id": 888,
                        "trader_id": 1001
                    }
                    {
                        "order_id": 1741416550000001,
                        "status": "PARTIAL_FILLED",
                        "filled_qty": 100,
                        "remaining_qty": 100,
                        "avg_price": 10050
                    }
        Websocket接口：
            暂时不实现，以后再写。
    响应：
        HTTP即时响应：
        {
            "status": "ACCEPTED",
            "order_id": 1741416550000001, 
            "timestamp": 1741416550
        }
        成交回报：
        {
            "response_type": "TRADE_RESPONSE",
            "order_id": 1741416550000001,
            "trader_id": ,
            "symbol_id": ,
            "side": ,
            "filled_qty": 100,
            "price": 10050,
            "status": "FILLED"
        }
        行情推送(5档):
        {
            "response_type": "MARKET_DATA_SNAPSHOT",
            "symbol_id": 888,
            "timestamp": 1741416550,
            "bids": [
                {"price": 10049, "qty": 500},
                {"price": 10048, "qty": 300},
                {"price": 10047, "qty": 1200},
                {"price": 10046, "qty": 800},
                {"price": 10045, "qty": 100}
            ],
            "asks": [
                {"price": 10051, "qty": 400},
                {"price": 10052, "qty": 600},
                {"price": 10053, "qty": 200},
                {"price": 10054, "qty": 900},
                {"price": 10055, "qty": 150}
            ]
        }
    输入队列：
        下单、撤单OrderQueue：
            打标签：负责给用户请求打标签（tag），标签 = 时间戳（timestamp） + 标的编号（symbol_id） + 递增序列号（递增序列号怎么获取？原子变量）
            定序：然后异步地把请求(SubmitRequest、CancelRequest)加入输入无锁队列（RequestQueue）。
        查单OrderStatusQueue请求(QueryRequest)：
            查单(query_order)：在网关模块维护一个std::unordered_map(order_id, OrderStatus)，map监听TradeResponseQueue来实时更新，用户查单时从这里获取订单状态，然后异步地放入订单状态无锁队列（OrderStatusQueue），由另一个线程去进行分发。
    输出队列：
        成交回报队列(TradeResponseQueue(TradeResponse))：创建一个消费者线程专门分发成交回报。
            成交回报应包含字段：response_type, order_id, trader_id, symbol_id, side, filled_qty, price, status
        行情推送队列(MarketDataQueue(MarketDataResponse))：创建一个消费者线程专门分发行情推送。
            行情推送应包含字段：response_type, symbol_id, timestamp, bids(price, qty), asks(price, qty).


4.订单簿（OrderBook）：
    用vector+list+unordered_map实现，price(int64_t)为索引，price的精度为0.01元，考虑到A股的涨跌停板一般在10%，所以价格跳跃不会很大，所以vector不会占用很多内存。开盘前加载昨日收盘价，乘1.1设置为涨停板，乘0.9设置为跌停板。
    前收盘价(prev_close_price)：计算公式有些复杂，暂时设置为一个定值。
    今日涨停板：前收盘价 * 11 / 10 (注意处理四舍五入)
    今日跌停板：前收盘价 * 9 / 10 (注意处理四舍五入)
    索引 = price - lower_limit_price
    price相同时用list存储订单：
        1.撮合引擎插入：直接在list尾部插入。
        2.撮合引擎取出：直接从list首部取出。
        3.撮合引擎撤单：用unordered_map快速查询，如果查到了，就撤单。如果没有查到，说明已经成交了，就返回REJECTED。
    用unordered_map存储每个订单对象的迭代器，用于快速撤单和查单。

5.单线程撮合引擎（MatchingEngine）：
    这是核心，撮合引擎会从输入无锁队列中取出请求并尝试撮合，如果不能撮合就加入订单簿。
    应该支持下单（submit_order）、撤单(cancel_order)。
    撮合引擎每秒生成5档盘口快照（这里的5档是指有单的价格档位，如果所剩单不足5档，往后读到5档并置零即可），异步发送给行情推送无锁队列（MarketDataQueue）。
    风险控制：
        1.大额市价单保护：用户下单撮合只能吃5档订单，不能超出。
        2.防止价格超出：用户下单时检查价格是否大于涨停板或小于跌停板，如果是的话就拒绝（不知道这个规则是否合理？）
        3.防止自成交：用户下单后撮合时检查trader_id是否相同，如果能够撮合成功但是trader_id相同，立即停止撮合，但不撤回之前撮合成功的成交。剩余的订单状态改为REJECTED，并发送给TradeResponseQueue。
    核心成员：
        下单撤单队列的引用 order_queue 撮合引擎是消费者
        成交回报队列的引用 trade_response_queue 撮合引擎是生产者
        行情推送队列的引用 market_data_queue 撮合引擎是生产者
        订单簿 OrderBook 维护买卖双方的订单
    核心函数：
        构造函数: 传入引用order_queue, trade_response_queue, market_data_queue
        死循环运行 run() : 轮询 order_queue , 如果有订单则 try_match()。
        尝试撮合 try_match() ：如果撮合成功，生成成交回报，如果有剩余或者没能撮合成功，加入订单簿。

6.主函数：
    创建order_queue, trade_response_queue, market_data_queue
    创建撮合引擎对象，传入队列引用
    创建网关对象，传入队列引用

6.成交回报(存在于网关模块中)：当撮合完毕时，异步地将结果放入成交回报无锁队列（TradeResponseQueue），由网关取出并发回给用户。

7.行情推送(存在于网关模块中)：用发布订阅模式，用户订阅之后，由网关从MarketDataQueue取数据分发。

日志：用spdlog同时打印到控制台和输出到txt文件。

命名风格：函数名、变量名用蛇形。类名、结构体名用大驼峰。
