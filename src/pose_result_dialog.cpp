#include "pose_result_dialog.h"
#include "ui_pose_result_dialog.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QWheelEvent>

#include <opencv2/geometry/3d.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {

struct Line3d {
    cv::Vec3d start;
    cv::Vec3d end;
    QColor color;
    qreal width = 1.0;
};

struct Label3d {
    cv::Vec3d position;
    QString text;
    QColor color;
};

cv::Matx33d rotationMatrix(const cv::Vec3d& rotationVector)
{
    cv::Matx33d rotation;
    cv::Rodrigues(rotationVector, rotation);
    return rotation;
}

struct ViewProjection {
    explicit ViewProjection(double yawDegrees, double pitchDegrees,
                            bool shouldFlipZ)
        : flipZ(shouldFlipZ)
    {
        constexpr double kDegreesToRadians =
            3.14159265358979323846 / 180.0;
        const double yaw = yawDegrees * kDegreesToRadians;
        const double pitch = pitchDegrees * kDegreesToRadians;
        cosineYaw = std::cos(yaw);
        sineYaw = std::sin(yaw);
        cosinePitch = std::cos(pitch);
        sinePitch = std::sin(pitch);
    }

    QPointF project(const cv::Vec3d& point) const
    {
        const double z = flipZ ? -point[2] : point[2];
        const double rotatedX =
            cosineYaw * point[0] - sineYaw * point[1];
        const double rotatedY =
            sineYaw * point[0] + cosineYaw * point[1];
        return {rotatedX,
                sinePitch * rotatedY - cosinePitch * z};
    }

    double cosineYaw = 1.0;
    double sineYaw = 0.0;
    double cosinePitch = 1.0;
    double sinePitch = 0.0;
    bool flipZ = false;
};

void appendAxes(std::vector<Line3d>& lines, std::vector<Label3d>& labels,
                double length, bool reverseZ)
{
    const cv::Vec3d xEnd{length, 0.0, 0.0};
    const cv::Vec3d yEnd{0.0, length, 0.0};
    const cv::Vec3d zEnd{0.0, 0.0, reverseZ ? -length : length};
    const QColor xColor(210, 55, 55);
    const QColor yColor(50, 160, 85);
    const QColor zColor(45, 105, 210);
    lines.push_back({{}, xEnd, xColor, 2.0});
    lines.push_back({{}, yEnd, yColor, 2.0});
    lines.push_back({{}, zEnd, zColor, 2.0});
    labels.push_back({xEnd, QStringLiteral("X"), xColor});
    labels.push_back({yEnd, QStringLiteral("Y"), yColor});
    labels.push_back({zEnd, QStringLiteral("Z"), zColor});
}

void appendBoard(std::vector<Line3d>& lines, std::vector<Label3d>& labels,
                 const cv::Matx33d& rotation, const cv::Vec3d& translation,
                 double width, double height, const QColor& color,
                 qreal lineWidth, const QString& label)
{
    const std::array<cv::Vec3d, 4> localCorners{
        cv::Vec3d{0.0, 0.0, 0.0},
        cv::Vec3d{width, 0.0, 0.0},
        cv::Vec3d{width, height, 0.0},
        cv::Vec3d{0.0, height, 0.0},
    };
    std::array<cv::Vec3d, 4> corners;
    for (size_t index = 0; index < corners.size(); ++index) {
        corners[index] = rotation * localCorners[index] + translation;
    }
    for (size_t index = 0; index < corners.size(); ++index) {
        lines.push_back(
            {corners[index], corners[(index + 1) % corners.size()],
             color, lineWidth});
    }
    lines.push_back({corners[0], corners[2], color, lineWidth * 0.6});
    lines.push_back({corners[1], corners[3], color, lineWidth * 0.6});
    labels.push_back({corners[0], label, color});
}

void appendCamera(std::vector<Line3d>& lines, std::vector<Label3d>& labels,
                  const cv::Matx33d& cameraToWorld,
                  const cv::Vec3d& cameraPosition, double size,
                  const QColor& color, qreal lineWidth,
                  const QString& label)
{
    const std::array<cv::Vec3d, 4> imagePlane{
        cv::Vec3d{-size, -0.7 * size, 1.6 * size},
        cv::Vec3d{size, -0.7 * size, 1.6 * size},
        cv::Vec3d{size, 0.7 * size, 1.6 * size},
        cv::Vec3d{-size, 0.7 * size, 1.6 * size},
    };
    std::array<cv::Vec3d, 4> corners;
    for (size_t index = 0; index < corners.size(); ++index) {
        corners[index] =
            cameraToWorld * imagePlane[index] + cameraPosition;
        lines.push_back(
            {cameraPosition, corners[index], color, lineWidth});
    }
    for (size_t index = 0; index < corners.size(); ++index) {
        lines.push_back(
            {corners[index], corners[(index + 1) % corners.size()],
             color, lineWidth});
    }
    labels.push_back({cameraPosition, label, color});
}

QTableWidgetItem* numericItem(double value)
{
    auto* item = new QTableWidgetItem(QString::number(value, 'f', 5));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

}  // namespace

struct PoseVisualizationWidget::Geometry {
    std::vector<Line3d> lines;
    std::vector<Label3d> labels;
};

PoseVisualizationWidget::PoseVisualizationWidget(QWidget* parent)
    : QWidget(parent),
      geometry_(std::make_unique<Geometry>())
{
    setMinimumSize(480, 360);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("按住鼠标左键拖动旋转视角，滚轮缩放"));
}

PoseVisualizationWidget::~PoseVisualizationWidget() = default;

void PoseVisualizationWidget::setCalibrationResult(
    const CalibrationResult& result)
{
    result_ = result;
    highlightedPose_ = result_.poses.empty() ? -1 : 0;
    rebuildGeometry();
    update();
}

void PoseVisualizationWidget::setViewStyle(ViewStyle style)
{
    if (viewStyle_ == style) {
        return;
    }
    viewStyle_ = style;
    resetView();
    rebuildGeometry();
    update();
}

void PoseVisualizationWidget::setHighlightedPose(int index)
{
    if (highlightedPose_ == index) {
        return;
    }
    highlightedPose_ = index;
    rebuildGeometry();
    update();
}

QSize PoseVisualizationWidget::sizeHint() const
{
    return {680, 520};
}

void PoseVisualizationWidget::resetView()
{
    yawDegrees_ = 45.0;
    pitchDegrees_ =
        viewStyle_ == ViewStyle::PatternCentric ? 25.0 : 30.0;
    zoom_ = 1.0;
}

void PoseVisualizationWidget::rebuildGeometry()
{
    geometry_->lines.clear();
    geometry_->labels.clear();
    if (!result_.success || result_.poses.empty()) {
        return;
    }

    const double boardWidth =
        result_.options.boardSize.width() * result_.options.squareSize;
    const double boardHeight =
        result_.options.boardSize.height() * result_.options.squareSize;
    const double referenceSize = std::max(boardWidth, boardHeight);
    const size_t poseCount = result_.poses.size();
    const bool patternCentric =
        viewStyle_ == ViewStyle::PatternCentric;
    geometry_->lines.reserve(
        (patternCentric ? 9U : 11U)
        + poseCount * (patternCentric ? 8U : 6U));
    geometry_->labels.reserve(4U + poseCount);
    appendAxes(geometry_->lines, geometry_->labels,
               referenceSize * 0.55, patternCentric);

    if (!patternCentric) {
        appendCamera(geometry_->lines, geometry_->labels,
                     cv::Matx33d::eye(), {}, referenceSize * 0.08,
                     QColor(45, 50, 60), 2.0,
                     QStringLiteral("Camera"));
        for (size_t index = 0; index < poseCount; ++index) {
            const CalibrationPose& pose = result_.poses[index];
            const bool highlighted =
                static_cast<int>(index) == highlightedPose_;
            const QColor color =
                highlighted ? QColor(235, 130, 35)
                            : QColor(50, 115, 205, 175);
            appendBoard(geometry_->lines, geometry_->labels,
                        rotationMatrix(pose.rotationVector),
                        pose.translationVector, boardWidth, boardHeight,
                        color, highlighted ? 3.0 : 1.2,
                        QString::number(index + 1));
        }
        return;
    }

    appendBoard(geometry_->lines, geometry_->labels,
                cv::Matx33d::eye(), {}, boardWidth, boardHeight,
                QColor(45, 50, 60), 2.0,
                QStringLiteral("Calibration Board"));
    for (size_t index = 0; index < poseCount; ++index) {
        const CalibrationPose& pose = result_.poses[index];
        const cv::Matx33d boardToCamera =
            rotationMatrix(pose.rotationVector);
        const cv::Matx33d cameraToBoard = boardToCamera.t();
        const cv::Vec3d cameraPosition =
            -(cameraToBoard * pose.translationVector);
        const bool highlighted =
            static_cast<int>(index) == highlightedPose_;
        const QColor color =
            highlighted ? QColor(235, 130, 35)
                        : QColor(50, 115, 205, 175);
        appendCamera(geometry_->lines, geometry_->labels,
                     cameraToBoard, cameraPosition,
                     referenceSize * 0.08, color,
                     highlighted ? 3.0 : 1.2,
                     QString::number(index + 1));
    }
}

void PoseVisualizationWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(247, 249, 252));
    painter.setPen(QPen(QColor(205, 211, 220), 1.0));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (!result_.success || result_.poses.empty()) {
        painter.setPen(QColor(90, 95, 105));
        painter.drawText(rect(), Qt::AlignCenter, tr("暂无可显示的位姿结果"));
        return;
    }

    const bool patternCentric =
        viewStyle_ == ViewStyle::PatternCentric;
    const std::vector<Line3d>& lines = geometry_->lines;
    const std::vector<Label3d>& labels = geometry_->labels;

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    const ViewProjection projection(yawDegrees_, pitchDegrees_,
                                    patternCentric);
    const auto projectPoint = [&](const cv::Vec3d& point) {
        return projection.project(point);
    };
    const auto includePoint = [&](const cv::Vec3d& point) {
        const QPointF projected = projectPoint(point);
        minX = std::min(minX, projected.x());
        minY = std::min(minY, projected.y());
        maxX = std::max(maxX, projected.x());
        maxY = std::max(maxY, projected.y());
    };
    for (const Line3d& line : lines) {
        includePoint(line.start);
        includePoint(line.end);
    }

    constexpr qreal kMargin = 48.0;
    const double spanX = std::max(1.0, maxX - minX);
    const double spanY = std::max(1.0, maxY - minY);
    const double scale =
        std::max(0.001,
                 std::min((width() - 2.0 * kMargin) / spanX,
                          (height() - 2.0 * kMargin) / spanY))
        * zoom_;
    const QPointF projectedCenter((minX + maxX) * 0.5,
                                  (minY + maxY) * 0.5);
    const QPointF widgetCenter(width() * 0.5, height() * 0.5);
    const auto mapPoint = [&](const cv::Vec3d& point) {
        const QPointF projected = projectPoint(point);
        return widgetCenter + (projected - projectedCenter) * scale;
    };

    for (const Line3d& line : lines) {
        painter.setPen(QPen(line.color, line.width, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(mapPoint(line.start), mapPoint(line.end));
    }
    for (const Label3d& label : labels) {
        painter.setPen(label.color);
        painter.drawText(mapPoint(label.position) + QPointF(5.0, -5.0),
                         label.text);
    }

    painter.setPen(QColor(70, 75, 85));
    painter.drawText(
        QRectF(12.0, 10.0, width() - 24.0, 24.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        viewStyle_ == ViewStyle::CameraCentric
            ? tr("Camera-Centric：固定相机，显示标定板姿态 · 拖动旋转 / 滚轮缩放")
            : tr("Pattern-Centric：固定标定板，显示相机姿态 · 拖动旋转 / 滚轮缩放"));
}

void PoseVisualizationWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastMousePosition_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PoseVisualizationWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ && event->buttons().testFlag(Qt::LeftButton)) {
        const QPointF delta = event->position() - lastMousePosition_;
        lastMousePosition_ = event->position();
        yawDegrees_ += delta.x() * 0.5;
        pitchDegrees_ =
            std::clamp(pitchDegrees_ + delta.y() * 0.4, -85.0, 85.0);
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PoseVisualizationWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PoseVisualizationWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    zoom_ = std::clamp(
        zoom_ * std::pow(1.0015, static_cast<double>(delta)),
        0.25, 5.0);
    update();
    event->accept();
}

PoseResultDialog::PoseResultDialog(const CalibrationResult& result,
                                   QWidget* parent)
    : QDialog(parent),
      ui_(std::make_unique<Ui::PoseResultDialog>())
{
    ui_->setupUi(this);
    ui_->poseVisualization->setCalibrationResult(result);
    ui_->summaryLabel->setText(
        tr("%1 个有效位姿 · RMS %2 px · 平移单位：mm")
            .arg(result.poses.size())
            .arg(result.rmsError, 0, 'f', 3));
    populatePoseTable(result);

    ui_->poseSplitter->setStretchFactor(0, 3);
    ui_->poseSplitter->setStretchFactor(1, 2);
    ui_->poseTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    for (int column = 1; column < ui_->poseTable->columnCount(); ++column) {
        ui_->poseTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }

    connect(ui_->viewStyleCombo,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                ui_->poseVisualization->setViewStyle(
                    index == 0
                        ? PoseVisualizationWidget::ViewStyle::CameraCentric
                        : PoseVisualizationWidget::ViewStyle::PatternCentric);
            });
    connect(ui_->poseTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                ui_->poseVisualization->setHighlightedPose(currentRow);
            });
    connect(ui_->buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    if (!result.poses.empty()) {
        ui_->poseTable->selectRow(0);
    }
}

PoseResultDialog::~PoseResultDialog() = default;

void PoseResultDialog::populatePoseTable(const CalibrationResult& result)
{
    ui_->poseTable->setRowCount(static_cast<int>(result.poses.size()));
    for (size_t index = 0; index < result.poses.size(); ++index) {
        const CalibrationPose& pose = result.poses[index];
        const int row = static_cast<int>(index);
        auto* imageItem =
            new QTableWidgetItem(QFileInfo(pose.imagePath).fileName());
        imageItem->setToolTip(pose.imagePath);
        ui_->poseTable->setItem(row, 0, imageItem);
        for (int axis = 0; axis < 3; ++axis) {
            ui_->poseTable->setItem(
                row, axis + 1, numericItem(pose.rotationVector[axis]));
            ui_->poseTable->setItem(
                row, axis + 4, numericItem(pose.translationVector[axis]));
        }
        ui_->poseTable->setItem(
            row, 7, numericItem(pose.reprojectionError));
    }
}
