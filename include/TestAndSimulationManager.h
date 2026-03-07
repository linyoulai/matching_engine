#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// 测试用例类型
enum class TestCaseType {
    UNIT,
    INTEGRATION,
    PERFORMANCE,
    STRESS,
    SIMULATION
};

// 测试用例结构体
struct TestCase {
    std::string name;
    TestCaseType type;
    std::string description;
    std::function<void()> run;
    bool enabled = true;
};

// 仿真场景结构体
struct SimulationScenario {
    std::string name;
    std::string description;
    std::function<void()> setup;
    std::function<void()> run;
    std::function<void()> teardown;
};

// 测试与仿真管理器
class TestAndSimulationManager {
public:
    explicit TestAndSimulationManager();
    // 添加/移除测试用例
    void addTestCase(const TestCase& test_case);
    void removeTestCase(const std::string& name);
    // 运行单个/全部测试
    void runTestCase(const std::string& name);
    void runAllTests();
    // 添加/移除仿真场景
    void addSimulationScenario(const SimulationScenario& scenario);
    void removeSimulationScenario(const std::string& name);
    // 运行仿真场景
    void runSimulation(const std::string& name);
    // 日志回调
    void setLogCallback(const std::function<void(const std::string&)>& cb);
    void log(const std::string& msg) const;
private:
    std::vector<TestCase> test_cases_;
    std::vector<SimulationScenario> simulation_scenarios_;
    // ...
};
