#pragma once

#include <QString>
#include <QStringList>

#include <QtGlobal>

/**
 * @brief 图片质量预检产生的统计量和提示
 */
struct ImageQualityResult
{
    bool readable = false; ///< 是否成功读取图片
    double sharpness = 0.0; ///< 拉普拉斯方差，数值越低通常越模糊
    double meanBrightness = 0.0; ///< 平均灰度，取值范围为 0–255
    double clippedRatio = 0.0; ///< 近黑或近白像素占总像素的比例
    quint64 similarityHash = 0; ///< 用于比较画面相似度的 64 位差值哈希
    QStringList warnings; ///< 根据预检阈值生成的用户提示
};

/**
 * @brief 对单张图片执行低成本质量预检
 *
 * @param filePath 输入图片路径
 *
 * @return 图片的质量指标；无法读取时 readable 为 false 并包含错误提示
 */
ImageQualityResult analyzeImageQuality(const QString &filePath);
