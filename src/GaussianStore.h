#pragma once
#include "si/GaussianParser.hpp"
#include <QJsonObject>
namespace youngkly {
si::GaussianResult cachedGaussian(const QString &path, const QString &cache, bool force,
                                 QJsonObject &metrics);
}
