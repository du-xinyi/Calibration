#pragma once

#include "calibration.h"

#include <QDialog>
#include <QPointF>
#include <QStringList>
#include <QWidget>

#include <memory>

class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QWheelEvent;

namespace Ui {
class PoseResultDialog;
}

/**
 * @brief 绘制类似 MATLAB showExtrinsics 的相机/标定板外参视图。
 */
class PoseVisualizationWidget final : public QWidget {
public:
    /**
     * @brief 位姿场景的参考坐标系
     */
    enum class ViewStyle {
        CameraCentric,  ///< 固定相机坐标系，绘制各标定板位姿
        PatternCentric, ///< 固定标定板坐标系，绘制各相机位姿
    };

    /**
     * @brief 创建可交互的位姿可视化控件
     *
     * @param parent 父控件，可为 nullptr
     */
    explicit PoseVisualizationWidget(QWidget* parent = nullptr);

    /**
     * @brief 销毁控件及其内部几何缓存
     */
    ~PoseVisualizationWidget() override;

    /**
     * @brief 设置待显示的标定结果并重建场景
     *
     * @param result 包含逐图外参的成功标定结果
     */
    void setCalibrationResult(const CalibrationResult& result);

    /**
     * @brief 切换场景的参考坐标系并重置观察视角
     *
     * @param style 新的参考坐标系
     */
    void setViewStyle(ViewStyle style);

    /**
     * @brief 高亮指定的原始位姿索引
     *
     * @param index result.poses 中的索引；负值表示不高亮
     */
    void setHighlightedPose(int index);

    /**
     * @brief 返回布局系统使用的建议显示尺寸
     *
     * @return 建议的控件尺寸
     */
    QSize sizeHint() const override;

protected:
    /// 将三维几何正交投影到控件平面并绘制
    void paintEvent(QPaintEvent* event) override;
    /// 开始鼠标拖动旋转
    void mousePressEvent(QMouseEvent* event) override;
    /// 根据鼠标位移更新观察角度
    void mouseMoveEvent(QMouseEvent* event) override;
    /// 结束鼠标拖动旋转
    void mouseReleaseEvent(QMouseEvent* event) override;
    /// 根据滚轮增量缩放场景
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Geometry;

    void resetView();
    void rebuildGeometry();

    // === 场景数据 ===
    CalibrationResult result_;                    ///< 当前显示的标定结果副本
    std::unique_ptr<Geometry> geometry_;           ///< 投影前的线段与标签缓存
    ViewStyle viewStyle_ = ViewStyle::CameraCentric; ///< 当前参考坐标系
    int highlightedPose_ = 0;                     ///< result_.poses 中的高亮索引

    // === 交互视角 ===
    QPointF lastMousePosition_;      ///< 上一次拖动事件的鼠标位置
    double yawDegrees_ = 45.0;       ///< 水平旋转角度
    double pitchDegrees_ = 30.0;     ///< 垂直旋转角度，限制在 ±85°
    double zoom_ = 1.0;              ///< 用户缩放倍率，限制在 0.25–5.0
    bool dragging_ = false;          ///< 是否正在用鼠标左键旋转视角
};

/**
 * @brief 展示全部有效标定图片的外参数值与位姿分布。
 */
class PoseResultDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief 创建位姿结果对话框
     *
     * @param result 包含逐图外参的成功标定结果
     * @param parent 父窗口，可为 nullptr
     */
    explicit PoseResultDialog(const CalibrationResult& result,
                              QWidget* parent = nullptr);

    /**
     * @brief 销毁对话框及其界面资源
     */
    ~PoseResultDialog() override;

signals:
    /**
     * @brief 请求主窗口移除勾选图片并使用剩余图片重新标定
     *
     * @param imagePaths 待排除的图片路径
     * @param baselineRms 排除图片前的整体 RMS，用于结果对比
     */
    void excludeImagesRequested(const QStringList& imagePaths,
                                double baselineRms);

private:
    void populatePoseTable(const CalibrationResult& result);
    QStringList checkedImagePaths() const;
    void updateExcludeButton();

    std::unique_ptr<Ui::PoseResultDialog> ui_;
    QPushButton* excludeButton_ = nullptr; ///< 排除所选项并重新标定的动作按钮
    double baselineRms_ = 0.0;             ///< 创建对话框时的整体 RMS 基线
};
