#pragma once

#include "calibration.h"

#include <QDialog>
#include <QPointF>
#include <QWidget>

#include <memory>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace Ui {
class PoseResultDialog;
}

/**
 * @brief 绘制类似 MATLAB showExtrinsics 的相机/标定板外参视图。
 */
class PoseVisualizationWidget final : public QWidget {
public:
    enum class ViewStyle {
        CameraCentric,
        PatternCentric,
    };

    explicit PoseVisualizationWidget(QWidget* parent = nullptr);
    ~PoseVisualizationWidget() override;

    void setCalibrationResult(const CalibrationResult& result);
    void setViewStyle(ViewStyle style);
    void setHighlightedPose(int index);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Geometry;

    void resetView();
    void rebuildGeometry();

    CalibrationResult result_;
    std::unique_ptr<Geometry> geometry_;
    ViewStyle viewStyle_ = ViewStyle::CameraCentric;
    int highlightedPose_ = 0;
    QPointF lastMousePosition_;
    double yawDegrees_ = 45.0;
    double pitchDegrees_ = 30.0;
    double zoom_ = 1.0;
    bool dragging_ = false;
};

/**
 * @brief 展示全部有效标定图片的外参数值与位姿分布。
 */
class PoseResultDialog final : public QDialog {
public:
    explicit PoseResultDialog(const CalibrationResult& result,
                              QWidget* parent = nullptr);
    ~PoseResultDialog() override;

private:
    void populatePoseTable(const CalibrationResult& result);

    std::unique_ptr<Ui::PoseResultDialog> ui_;
};
