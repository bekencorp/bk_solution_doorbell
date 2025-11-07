#pragma once

/**
 * @brief 运行所有测试用例
 *
 * 包含8个测试：
 * 1. 基本功能测试
 * 2. 多消费者测试
 * 3. 慢速消费者保护测试
 * 4. 动态注册/注销测试
 * 5. 超时保护测试
 * 6. 性能对比测试
 * 7. 压力测试
 * 8. 边界条件测试
 */
void frame_queue_v2_run_all_tests(void);

/**
 * @brief 运行单个测试
 *
 * @param test_id 测试ID（1-8）
 */
void frame_queue_v2_run_single_test(uint32_t test_id);

/**
 * @brief 快速测试（只运行核心测试）
 *
 * 包含3个核心测试：
 * 1. 基本功能测试
 * 2. 多消费者测试
 * 3. 慢速消费者保护测试
 */
void frame_queue_v2_run_quick_tests(void);


