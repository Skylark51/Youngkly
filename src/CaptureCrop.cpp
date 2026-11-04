#include "CaptureCrop.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace youngkly {
CaptureCrop captureCrop(const QVector<QImage> &images) {
    if (images.isEmpty() || images.front().isNull())
        throw std::runtime_error("Cannot crop an empty capture");
    const auto size = images.front().size();
    QRect bounds;
    for (const auto &image : images) {
        if (image.size() != size)
            throw std::runtime_error("Capture frames must have the same dimensions");
        const auto pixels = image.convertToFormat(QImage::Format_ARGB32);
        int left = size.width(), right = -1, top = size.height(), bottom = -1;
        for (int y = 0; y < pixels.height(); ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
            for (int x = 0; x < pixels.width(); ++x) {
                const auto pixel = line[x];
                // Composite on white when transparent. Retain pale shadows and hydrogen outlines.
                const int contrast =
                    (255 - std::min({qRed(pixel), qGreen(pixel), qBlue(pixel)})) * qAlpha(pixel);
                if (contrast < 3 * 255)
                    continue;
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
        if (right >= left)
            bounds = bounds.united(QRect(QPoint(left, top), QPoint(right, bottom)));
    }
    if (bounds.isEmpty())
        return {images.front().rect(), 0};
    const int margin = std::max(4, int(std::ceil(std::max(bounds.width(), bounds.height()) * 0.015)));
    return {bounds.adjusted(-margin, -margin, margin, margin).intersected(images.front().rect()), margin};
}
} // namespace youngkly
