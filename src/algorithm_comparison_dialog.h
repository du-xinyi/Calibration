#pragma once

#include "calibration.h"

#include <QDialog>

#include <vector>

/**
 * @brief 展示同一数据集在不同相机模型与角点检测器下的标定结果。
 */
class AlgorithmComparisonDialog : public QDialog {
public:
    explicit AlgorithmComparisonDialog(
        std::vector<CalibrationResult> results,
        QWidget* parent = nullptr);
};
