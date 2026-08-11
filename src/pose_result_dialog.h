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
 * @brief 将逐图外参绘制为可旋转、可缩放的三维线框场景
 */
class PoseVisualizationWidget final : public QWidget {
public:
    /**
     * @brief 位姿场景中保持静止的参考对象
     */
    enum class ViewStyle {
        CameraCentric, ///< 固定相机并显示各标定板
        PatternCentric, ///< 固定标定板并显示各相机
    };

    /**
     * @brief 创建位姿绘制控件
     *
     * @param parent Qt 对象树中的父控件，允许为空
     */
    explicit PoseVisualizationWidget(QWidget* parent = nullptr);

    /**
     * @brief 释放内部场景几何数据
     */
    ~PoseVisualizationWidget() override;

    /**
     * @brief 替换当前标定结果并重新生成场景
     *
     * @param result 含逐图外参的标定结果
     */
    void setCalibrationResult(const CalibrationResult& result);

    /**
     * @brief 更改场景参考对象
     *
     * @param style 新视图模式
     */
    void setViewStyle(ViewStyle style);

    /**
     * @brief 设置需要强调显示的位姿
     *
     * @param index result.poses 中的下标，负值表示取消高亮
     */
    void setHighlightedPose(int index);

    /**
     * @brief 提供布局使用的首选控件尺寸
     *
     * @return 首选宽高
     */
    QSize sizeHint() const override;

protected:
    /**
     * @brief 投影并绘制当前三维场景
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief 记录旋转交互的起始位置
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief 将拖动距离换算为观察角度
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief 结束旋转交互
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief 调整场景缩放倍率
     */
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Geometry;

    void resetView();
    void rebuildGeometry();

    // === 可视化数据 ===
    CalibrationResult result_; ///< 当前场景使用的标定结果副本
    std::unique_ptr<Geometry> geometry_;
    ViewStyle viewStyle_ = ViewStyle::CameraCentric; ///< 当前固定的参考对象
    int highlightedPose_ = 0; ///< 需要使用强调样式的位姿下标

    // === 观察器交互状态 ===
    QPointF lastMousePosition_; ///< 最近一次拖动位置
    double yawDegrees_ = 45.0; ///< 水平观察角
    double pitchDegrees_ = 30.0; ///< 垂直观察角
    double zoom_ = 1.0; ///< 额外缩放倍率，范围为 0.25–5.0
    bool dragging_ = false; ///< 鼠标左键是否正在控制视角
};

/**
 * @brief 联动展示逐图外参数值、误差排序和三维位姿
 */
class PoseResultDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief 使用一次标定结果创建位姿窗口
     *
     * @param result 含有效外参的标定结果
     * @param parent Qt 对象树中的父窗口，允许为空
     */
    explicit PoseResultDialog(const CalibrationResult& result,
                              QWidget* parent = nullptr);

    /**
     * @brief 销毁位姿窗口
     */
    ~PoseResultDialog() override;

signals:
    /**
     * @brief 请求调用方移除指定图片并重新求解
     *
     * @param imagePaths 用户选中的图片路径
     * @param baselineRms 移除图片前的整体 RMS
     */
    void excludeImagesRequested(const QStringList& imagePaths,
                                double baselineRms);

private:
    void populatePoseTable(const CalibrationResult& result);
    QStringList checkedImagePaths() const;
    void updateExcludeButton();

    std::unique_ptr<Ui::PoseResultDialog> ui_;
    QPushButton* excludeButton_ = nullptr; ///< 由对话框按钮盒管理的排除动作
    double baselineRms_ = 0.0; ///< 当前结果的误差比较基线
};
