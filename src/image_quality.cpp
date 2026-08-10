#include "image_quality.h"

#include <QObject>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>

ImageQualityResult analyzeImageQuality(const QString& filePath)
{
    ImageQualityResult result;
    cv::Mat image = cv::imread(filePath.toStdString(), cv::IMREAD_GRAYSCALE);
    if (image.empty()) {
        result.warnings << QObject::tr("无法读取");
        return result;
    }
    result.readable = true;
    if (image.cols > 640 || image.rows > 640) {
        const double scale = 640.0 / std::max(image.cols, image.rows);
        cv::resize(image, image, {}, scale, scale, cv::INTER_AREA);
    }

    cv::Scalar mean;
    cv::Scalar deviation;
    cv::meanStdDev(image, mean, deviation);
    result.meanBrightness = mean[0];
    cv::Mat laplacian;
    cv::Laplacian(image, laplacian, CV_64F);
    cv::meanStdDev(laplacian, mean, deviation);
    result.sharpness = deviation[0] * deviation[0];

    cv::Mat clipped = (image <= 5) | (image >= 250);
    result.clippedRatio = static_cast<double>(cv::countNonZero(clipped))
                          / static_cast<double>(image.total());
    if (result.sharpness < 50.0) {
        result.warnings << QObject::tr("可能模糊");
    }
    if (result.meanBrightness < 35.0) {
        result.warnings << QObject::tr("曝光不足");
    } else if (result.meanBrightness > 220.0) {
        result.warnings << QObject::tr("可能过曝");
    }
    if (result.clippedRatio > 0.25) {
        result.warnings << QObject::tr("亮暗区域裁切较多");
    }

    cv::Mat hashImage;
    cv::resize(image, hashImage, cv::Size(9, 8), 0.0, 0.0, cv::INTER_AREA);
    quint64 hash = 0;
    for (int row = 0; row < hashImage.rows; ++row) {
        for (int col = 0; col < 8; ++col) {
            hash <<= 1U;
            if (hashImage.at<unsigned char>(row, col)
                > hashImage.at<unsigned char>(row, col + 1)) {
                hash |= 1U;
            }
        }
    }
    result.similarityHash = hash;
    return result;
}
