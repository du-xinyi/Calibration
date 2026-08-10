#pragma once

#include "calibration.h"

#include <QDialog>

#include <vector>

/**
 * @brief 展示同一数据集在不同相机模型与角点检测器下的标定结果。
 */
class AlgorithmComparisonDialog : public QDialog {
public:
    /**
     * @brief 创建算法结果对比对话框
     *
     * @param results 待展示的标定结果；对话框会按成功状态和 RMS 排序
     * @param parent 父窗口，可为 nullptr
     */
    explicit AlgorithmComparisonDialog(
        std::vector<CalibrationResult> results,
        QWidget* parent = nullptr);
};
