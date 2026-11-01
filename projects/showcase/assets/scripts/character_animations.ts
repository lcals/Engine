export const walk_animation = "models/locomotions.fbx/animations/walk";

// 原始 walk 的 root/Hips 每周期 Z 位移 142.5300068，骨架缩放 0.01，时长 1.33333337 秒。
// 参考步速 = 142.5300068 × 0.01 / 1.33333337 ≈ 1.068975 m/s；导入时提取前向位移。
export const walk_reference_speed = 1.068975;
