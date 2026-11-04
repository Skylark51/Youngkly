#pragma once
#include <QImage>
#include <QRect>
#include <QVector>

namespace youngkly {
struct CaptureCrop {
    QRect rect;
    int marginPixels = 0;
};
// One rectangle for the still and every animation frame, in original image coordinates.
CaptureCrop captureCrop(const QVector<QImage> &images);
} // namespace youngkly
