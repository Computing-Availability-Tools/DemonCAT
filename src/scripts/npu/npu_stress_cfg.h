/* npu_stress_cfg.h — _npu_stress 满血/PWM 两套算子配置决策（纯 C，无 ACL 依赖）
 *
 * mode 字符串映射 aicore/aicpu/aivector 到算子类型与数据类型；
 * load_pct>=100（含未指定默认）走满血模式（大算子配置），
 * load_pct<100 走 PWM 占空比模式（现有行为）。
 * 本模块可被 C++ (ACL) 主程序 include，也可被 UT (C) 直接编译测试。
 */
#ifndef NPU_STRESS_CFG_H
#define NPU_STRESS_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

enum npu_stress_op {
    NPU_STRESS_MATMUL = 0, /* aclnnMatmul  (aicore)  */
    NPU_STRESS_TOPK = 1,   /* aclnnTopk    (aicpu)   */
    NPU_STRESS_ADD = 2,    /* aclnnAdd     (aivector) */
    NPU_STRESS_EXP = 3     /* aclnnExp     (aivector) */
};

enum npu_stress_dtype {
    NPU_STRESS_FP16 = 0,
    NPU_STRESS_FP32 = 1,
    NPU_STRESS_FP64 = 2
};

struct npu_stress_cfg {
    int op;         /* npu_stress_op    */
    int dtype;      /* npu_stress_dtype */
    int shape;      /* N×N 矩阵维度        */
    int fullpower;  /* 1=满血直跑(无 PWM), 0=PWM */
    float max_achievable; /* PWM 校准系数(满血路径不用): aicore 0.96, aicpu 0.94, aivector 0.84 */
};

/* 根据 mode("aicore"/"aicpu"/"aivector")、load_pct、size(=0 用默认) 返回算子配置 */
struct npu_stress_cfg npu_stress_cfg(const char *mode, int load_pct, int size);

#ifdef __cplusplus
}
#endif

#endif /* NPU_STRESS_CFG_H */
