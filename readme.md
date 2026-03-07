这是一个接近真实交易所的撮合引擎项目。

特别说明：
    1.此项目目前仅支持单个标的，如需支持多个标的，应该创建多个实例。
    2.不支持存储用户资金数据，用户资金数据应该由券商或结算中心管理。

模块包括：

1.订单（Order）：
    包含字段：order_id（直接用网关层打的tag），ts，price，qty，filled_qty, side, order_type, tif, order_status
    订单类型枚举（OrderType）：包括限价单（LIMIT），市价单（MARKET）。
    TimeInForce：tif包括GTC，IOC，FOK。
    订单状态（OrderStatus）：NEW，EXPIRED，PARTIAL_FILLED，FILLED，REJECTED。
    // 订单
    struct Order {};

    // 业务请求包
    struct RequestEnvelope {
        uint8_t type; // 0: 下单, 1: 撤单, 2: 查单
        union {
            OrderRequest order_req;
            CancelRequest cancel_req;
            QueryRequest query_req;
        } data;
    };

    struct QueryRequest {
        uint64_t tag;        // 8 字节
        uint32_t trader_id;  // 4 字节
        uint32_t symbol_id;  // 4 字节
    }; // 合计 16 字节，完美对齐

2.网关（Gateway）：
    打标签：负责给用户请求打标签（tag），标签 = 时间戳（timestamp） + 标的编号（symbol_id） + 递增序列号（递增序列号怎么获取？原子变量）
    定序：然后异步地加入输入无锁队列（RequestQueue）。
    查单(query_order)：在网关模块维护一个std::unordered_map<order_id, OrderStatus>，map监听TradeResponseQueue来实时更新，用户查单时从这里获取订单状态，然后异步地放入订单状态无锁队列（OrderStatusQueue），由另一个线程去进行分发。

4.订单簿（OrderBook）：
    用vector+list+unordered_map实现，price(int64_t)为索引，price的精度为0.01元，考虑到A股的涨跌停板一般在10%，所以价格跳跃不会很大，所以vector不会占用很多内存。开盘前加载昨日收盘价，乘1.1设置为涨停板，乘0.9设置为跌停板。
    price相同时用list存储订单：
        1.撮合引擎插入：直接在list尾部插入。
        2.撮合引擎取出：直接从list首部取出。
        3.撮合引擎撤单：用unordered_map快速查询，如果查到了，就撤单。如果没有查到，说明已经成交了，就返回REJECTED。
    用unordered_map存储每个订单对象的迭代器，用于快速撤单和查单。

5.撮合引擎（MatchingEngine）：
    这是核心，撮合引擎会从输入无锁队列中取出请求并尝试撮合，如果不能撮合就加入订单簿。
    应该支持下单（submit_order）、撤单(cancel_order)。
    撮合引擎每秒生成5档盘口快照（这里的5档是指有单的价格档位，如果所剩单不足5档，往后读到5档并置零即可），异步发送给行情推送无锁队列（MarketDataQueue）。
    风险控制：
        1.大额市价单保护：用户下单撮合只能吃5档订单，不能超出。
        2.防止价格超出：用户下单时检查价格是否大于涨停板或小于跌停板，如果是的话就拒绝（不知道这个规则是否合理？）
        3.防止自成交：用户下单后撮合时检查trader_id是否相同，如果能够撮合成功但是trader_id相同，立即停止撮合，但不撤回之前撮合成功的成交。剩余的订单状态改为REJECTED，并发送给TradeResponseQueue。

6.成交回报：当撮合完毕时，异步地将结果放入成交回报无锁队列（TradeResponseQueue），由网关取出并发回给用户。

7.行情推送：用发布订阅模式，用户订阅之后，由网关从MarketDataQueue取数据分发。

8.日志：用spdlog同时打印到控制台和输出到txt文件。

命名风格：函数名、变量名用蛇形。类名、结构体名用大驼峰。
