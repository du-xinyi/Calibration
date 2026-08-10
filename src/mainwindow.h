#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "calibration.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <memory>
#include <optional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressDialog;
class QScrollArea;
class QSpinBox;

class PoseResultDialog;

namespace Ui {
class MainWindow;
}

/**
 * @brief 相机标定主窗口，布局仿照 MATLAB Camera Calibrator。
 *
 * 顶部工具栏放置常用动作（添加图片、开始标定、导出参数、清除），
 * 中央区域用三栏分割器组织：左侧图片列表、中部大图预览、右侧标定选项，
 * 底部状态栏显示图片数量与重投影误差。
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    /// 析构前等待后台标定线程结束，避免 worker 访问已释放成员（SIGSEGV）。
    ~MainWindow() override;

    /// 批量载入图片路径（去重、生成缩略图）。
    void loadImages(const QStringList& files);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onAddImages();
    void onAddImagesFromFolder();
    void onCalibrate();
    void onShowPoses();
    void onExportParameters();
    void onClearAll();
    void onImageSelectionChanged();
    void onCameraModelChanged();
    void onBoardTypeChanged();
    void onCalibrationOptionsChanged();
    void onDisplayModeChanged();
    void onExcludeImagesAndRecalibrate(const QStringList& imagePaths,
                                       double baselineRms);

private:
    void buildMenuBar();
    void buildToolBar();
    void configureCentralWidget();
    void buildStatusBar();
    /// 根据图片数量、标定状态和运行状态刷新空提示及可用动作。
    void updateUiState();
    /// 当前输入已变化时丢弃旧标定结果，防止误导出不匹配的参数。
    void invalidateCalibrationResult();

    struct CalibrationComparison {
        double baselineRms = 0.0;
        int excludedImages = 0;
    };
    void startCalibration(
        std::optional<CalibrationComparison> comparison = std::nullopt);

    void showImage(const QString& filePath, bool* found = nullptr);
    void fitImageToView();
    /// 在 GUI 线程展示标定结果（成功弹报告、失败弹警告，刷新状态栏与预览）。
    void presentResult(const CalibrationResult& result);

    /// 收集界面中当前选择的完整标定参数。
    CalibrationOptions currentOptions() const;
    /// 收集列表中全部图片路径。
    QStringList collectFiles() const;

    std::unique_ptr<Ui::MainWindow> ui_;

    QListWidget* imageList_ = nullptr;
    QScrollArea* imageScroll_ = nullptr;
    QLabel* imageDisplay_ = nullptr;

    QComboBox* cameraModelCombo_ = nullptr;
    QComboBox* boardTypeCombo_ = nullptr;
    QSpinBox* boardColsSpin_ = nullptr;
    QSpinBox* boardRowsSpin_ = nullptr;
    QDoubleSpinBox* squareSizeSpin_ = nullptr;
    QDoubleSpinBox* markerSizeSpin_ = nullptr;
    QComboBox* dictionaryCombo_ = nullptr;
    QComboBox* methodCombo_ = nullptr;

    QCheckBox* skewCheck_ = nullptr;
    QCheckBox* tangentialCheck_ = nullptr;
    QSpinBox* radialCoeffSpin_ = nullptr;
    QCheckBox* showUndistortedCheck_ = nullptr;

    QLabel* statusImageCount_ = nullptr;
    QLabel* statusRmsError_ = nullptr;
    QAction* calibrateAction_ = nullptr;
    QAction* poseAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* clearAction_ = nullptr;

    QString lastDir_; ///< 记录上次打开的目录，作为下次对话框起点
    QPixmap currentPixmap_;

    Calibrator calibrator_;           ///< OpenCV 标定封装
    CalibrationResult lastResult_;    ///< 最近一次标定结果

    QProgressDialog* progressDialog_ = nullptr; ///< 标定进度对话框
    QFutureWatcher<CalibrationResult>* calibWatcher_ = nullptr; ///< 后台标定 future
    std::atomic<bool> calibCanceled_{false};    ///< 跨线程取消标志
    bool calibActive_ = false;                  ///< 标定进行中标志（防止重入）
    QTimer* debounceTimer_ = nullptr;           ///< 预览刷新的防抖计时器
    QPointer<PoseResultDialog> poseDialog_;     ///< 已打开的位姿对话框（防止重复）
};
