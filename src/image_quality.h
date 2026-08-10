#pragma once

#include <QString>
#include <QStringList>

#include <QtGlobal>

/**
 * @brief 单张图片的轻量质量预检结果。
 */
struct ImageQualityResult {
    bool readable = false;
    double sharpness = 0.0;       ///< 灰度拉普拉斯方差，越低通常越模糊
    double meanBrightness = 0.0;  ///< 平均灰度，范围 0-255
    double clippedRatio = 0.0;    ///< 接近纯黑或纯白的像素比例
    quint64 similarityHash = 0;   ///< 缩略图感知哈希，用于提示重复画面
    QStringList warnings;
};

/**
 * @brief 读取图片缩略图并检查模糊、曝光和大面积亮暗裁切。
 */
ImageQualityResult analyzeImageQuality(const QString& filePath);
