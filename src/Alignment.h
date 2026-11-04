#pragma once
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector>
namespace youngkly {
// Horn quaternion solution: proper rotation only, no reflection or scaling.
QMatrix4x4 rigidAlignment(const QVector<QVector3D> &source, const QVector<QVector3D> &target);
}
