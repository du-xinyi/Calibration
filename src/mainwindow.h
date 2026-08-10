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
    /**
     * @brief 创建并初始化相机标定主窗口
     *
     * @param parent 父窗口，可为 nullptr
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * @brief 销毁主窗口
     *
     * @details 析构前取消并等待后台任务，避免工作线程访问已经释放的窗口状态
     */
    ~MainWindow() override;

    /**
     * @brief 批量载入图片并生成缩略图与质量提示
     *
     * 已存在的路径和无法解码的文件不会加入列表
     *
     * @param files 待载入的图片路径
     */
    void loadImages(const QStringList& files);

protected:
    /**
     * @brief 窗口尺寸变化后重新缩放当前预览图
     *
     * @param event Qt 尺寸变化事件
     */
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onAddImages();
    void onAddImagesFromFolder();
    void onOpenProject();
    void onSaveProject();
    void onImportParameters();
    void onCalibrate();
    void onCompareAlgorithms();
    void onShowPoses();
    void onExportParameters();
    void onExportUndistortedImages();
    void onClearAll();
    void onImageSelectionChanged();
    void onCameraModelChanged();
    void onBoardTypeChanged();
    void onCalibrationOptionsChanged();
    void onDisplayModeChanged();
    void onExcludeImagesAndRecalibrate(const QStringList& imagePaths,
                                       double baselineRms);

private:
    // === 界面构建与状态同步 ===
    void buildMenuBar();
    void buildToolBar();
    void configureCentralWidget();
    void buildStatusBar();
    /// 根据图片数量、标定状态和运行状态刷新空提示及可用动作
    void updateUiState();
    /// 当前输入已变化时丢弃旧标定结果，防止导出不匹配的参数
    void invalidateCalibrationResult();

    /// 记录排除图片前的 RMS，用于重新标定后的结果对比
    struct CalibrationComparison {
        double baselineRms = 0.0; ///< 排除图片前的整体 RMS
        int excludedImages = 0;   ///< 从输入列表中排除的图片数
    };

    /// 启动后台标定；comparison 存在时在报告中追加排除前后的 RMS 变化
    void startCalibration(
        std::optional<CalibrationComparison> comparison = std::nullopt);

    void showImage(const QString& filePath, bool* found = nullptr);
    void fitImageToView();
    /// 在 GUI 线程展示标定结果，并刷新状态栏与预览
    void presentResult(const CalibrationResult& result);
    /// 将项目或参数文件中的选项同步到界面控件
    void applyOptions(const CalibrationOptions& options);
    /// 返回当前是否有后台标定、对比或批量导出任务
    bool isBusy() const;

    /// 收集界面中当前选择的完整标定参数
    CalibrationOptions currentOptions() const;
    /// 收集列表中全部图片路径
    QStringList collectFiles() const;

    std::unique_ptr<Ui::MainWindow> ui_;

    // === 图片列表与预览 ===
    QListWidget* imageList_ = nullptr;
    QScrollArea* imageScroll_ = nullptr;
    QLabel* imageDisplay_ = nullptr;

    // === 标定参数控件 ===
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

    // === 状态展示与用户动作 ===
    QLabel* statusImageCount_ = nullptr;
    QLabel* statusRmsError_ = nullptr;
    QAction* calibrateAction_ = nullptr;
    QAction* compareAction_ = nullptr;
    QAction* poseAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* batchExportAction_ = nullptr;
    QAction* openProjectAction_ = nullptr;
    QAction* saveProjectAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QAction* clearAction_ = nullptr;

    QString lastDir_; ///< 记录上次打开的目录，作为下次对话框起点
    QPixmap currentPixmap_; ///< 当前完整预览图，窗口缩放时据此重新生成显示图

    // === 标定结果 ===
    Calibrator calibrator_;           ///< OpenCV 标定封装
    CalibrationResult lastResult_;    ///< 最近一次标定结果
    bool lastResultImported_ = false;  ///< 导入参数不因追加待校正图片而失效

    // === 主标定后台任务 ===
    QProgressDialog* progressDialog_ = nullptr; ///< 标定进度对话框
    QFutureWatcher<CalibrationResult>* calibWatcher_ = nullptr; ///< 后台标定 future
    std::atomic<bool> calibCanceled_{false};    ///< 跨线程取消标志
    bool calibActive_ = false;                  ///< 标定进行中标志（防止重入）

    /// 汇总批量去畸变任务的成功数、跳过数和错误详情
    struct BatchUndistortSummary {
        int written = 0;     ///< 成功写出的图片数
        int skipped = 0;     ///< 读取、校验或写出失败的图片数
        QStringList errors;  ///< 各失败图片对应的错误说明
    };

    // === 算法对比与批量导出后台任务 ===
    QFutureWatcher<std::vector<CalibrationResult>>* comparisonWatcher_ = nullptr; ///< 算法对比任务观察器
    QFutureWatcher<BatchUndistortSummary>* batchWatcher_ = nullptr; ///< 批量去畸变任务观察器
    QProgressDialog* auxiliaryProgressDialog_ = nullptr; ///< 辅助后台任务共用的进度对话框
    std::atomic<bool> auxiliaryCanceled_{false}; ///< 辅助任务的跨线程取消标志
    bool auxiliaryActive_ = false;               ///< 辅助后台任务是否占用界面

    // === 界面生命周期 ===
    QTimer* debounceTimer_ = nullptr;           ///< 预览刷新的防抖计时器
    QPointer<PoseResultDialog> poseDialog_;     ///< 已打开的位姿对话框（防止重复）
};
