#pragma once

#include <QString>
#include <QStringList>

#include <QtGlobal>

/**
 * @brief 单张图片的轻量质量预检结果。
 */
struct ImageQualityResult {
    bool readable = false;        ///< 图片是否成功解码为灰度图
    double sharpness = 0.0;       ///< 灰度拉普拉斯方差，越低通常越模糊
    double meanBrightness = 0.0;  ///< 平均灰度，范围 0-255
    double clippedRatio = 0.0;    ///< 接近纯黑或纯白的像素比例
    quint64 similarityHash = 0;   ///< 缩略图感知哈希，用于提示重复画面
    QStringList warnings;         ///< 根据固定启发式阈值生成的质量提示
};

/**
 * @brief 读取图片缩略图并检查模糊、曝光和大面积亮暗裁切
 *
 * @param filePath 待检查的图片路径
 *
 * @return 质量指标、相似度哈希和启发式告警；读取失败时 readable 为 false
 */
ImageQualityResult analyzeImageQuality(const QString& filePath);
