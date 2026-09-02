/* tests/ut/test_npu_stress_cfg.c — 满血/PWM 算子配置决策单测
 *
 * npu_stress_cfg(mode, load_pct, size) 决定 _npu_stress 用哪套算子:
 *   - load_pct>=100(含未指定默认 100): 满血模式, 对齐华为 Python 参考脚本大算子
 *       aicore   FP32 matmul 5120(FP16 带宽受限仅 96%)
 *       aivector FP32 add   8500(PWM 用 exp 8192 仅 84%)
 *       aicpu    FP64 topk  2000
 *   - load_pct<100: PWM 模式(现有行为)
 *       aicore   FP16 matmul 5120 / aivector FP16 exp 8192 / aicpu FP64 topk 500
 * 目标: 满载饱和度 ≥99%(对齐 Python 参考), 由盘上 e2e 验证; 本单测锁定配置选择。
 */
#define _GNU_SOURCE
#include "test.h"
#include "../../src/scripts/npu/npu_stress_cfg.h"

/* ---- aicore ---- */
int test_cfg_aicore_fullpower(void) {
    struct npu_stress_cfg c = npu_stress_cfg("aicore", 100, 0);
    ASSERT_INT_EQ(c.fullpower, 1);
    ASSERT_INT_EQ(c.op, NPU_STRESS_MATMUL);
    ASSERT_INT_EQ(c.dtype, NPU_STRESS_FP32); /* 满血换 FP32 */
    ASSERT_INT_EQ(c.shape, 5120);
    return 0;
}
int test_cfg_aicore_fullpower_explicit_size(void) {
    struct npu_stress_cfg c = npu_stress_cfg("aicore", 100, 8192);
    ASSERT_INT_EQ(c.fullpower, 1);
    ASSERT_INT_EQ(c.shape, 8192);
    return 0;
}
int test_cfg_aicore_pwm(void) {
    struct npu_stress_cfg c = npu_stress_cfg("aicore", 50, 0);
    ASSERT_INT_EQ(c.fullpower, 0);
    ASSERT_INT_EQ(c.op, NPU_STRESS_MATMUL);
    ASSERT_INT_EQ(c.dtype, NPU_STRESS_FP16); /* PWM 保留 FP16 */
    ASSERT_INT_EQ(c.shape, 5120);
    return 0;
}
int test_cfg_aicore_default_is_fullpower(void) {
    /* load_pct 未指定默认 100 → 满血 */
    struct npu_stress_cfg c = npu_stress_cfg("aicore", 100, 0);
    ASSERT_INT_EQ(c.fullpower, 1);
    ASSERT_INT_EQ(c.dtype, NPU_STRESS_FP32);
    return 0;
}

/* ---- aivector ---- */
int test_cfg_aivector_fullpower(void) {
    struct npu_stress_cfg c = npu_stress_cfg("aivector", 100, 0);
    ASSERT_INT_EQ(c.fullpower, 1);
    ASSERT_INT_EQ(c.op, NPU_STRESS_ADD); /* 满血用 add 而非 exp */
    ASSERT_INT_EQ(c.dtype, NPU_STRESS_FP32);
    ASSERT_INT_EQ(c.shape, 8500);
    return 0;
}
int test_cfg_aivector_pwm(void) {
    struct npu_stress_cfg c = npu_stress_cfg("aivector", 50, 0);
    ASSERT_INT_EQ(c.fullpower, 0);
    ASSERT_INT_EQ(c.op, NPU_STRESS_EXP);
    ASSERT_INT_EQ(c.dtype, NPU_STRESS_FP16);
    ASSERT_INT_EQ(c.shape, 8192);
    return 0;
}

/* ---- aicpu ---- */
int test_cfg_aicpu_fullpower(void) {
    struct npu_stress_cfg c = npu_stress_cfg("aicpu", 100, 0);
    ASSERT_INT_EQ(c.fullpower, 1);
    ASSERT_INT_EQ(c.op, NPU_STRESS_TOPK);
    ASSERT_INT_EQ(c.dtype, NPU_STRESS_FP64);
    ASSERT_INT_EQ(c.shape, 2000); /* 满血 shape 2000(Python 参考) */
    return 0;
}
int test_cfg_aicpu_pwm(void) {
    struct npu_stress_cfg c = npu_stress_cfg("aicpu", 50, 0);
    ASSERT_INT_EQ(c.fullpower, 0);
    ASSERT_INT_EQ(c.op, NPU_STRESS_TOPK);
    ASSERT_INT_EQ(c.shape, 500);
    return 0;
}

/* ---- 未知 mode ---- */
int test_cfg_unknown_mode(void) {
    struct npu_stress_cfg c = npu_stress_cfg("bogus", 100, 0);
    ASSERT_INT_EQ(c.op, -1);
    ASSERT_INT_EQ(c.shape, 0);
    return 0;
}

int main(void) {
    RUN_TEST(test_cfg_aicore_fullpower);
    RUN_TEST(test_cfg_aicore_fullpower_explicit_size);
    RUN_TEST(test_cfg_aicore_pwm);
    RUN_TEST(test_cfg_aicore_default_is_fullpower);
    RUN_TEST(test_cfg_aivector_fullpower);
    RUN_TEST(test_cfg_aivector_pwm);
    RUN_TEST(test_cfg_aicpu_fullpower);
    RUN_TEST(test_cfg_aicpu_pwm);
    RUN_TEST(test_cfg_unknown_mode);
    return TEST_MAIN_RETURN();
}
