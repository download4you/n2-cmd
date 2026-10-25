#pragma once
#include <QImage>

// Applies a separable box blur to the image. Three passes approximate a
// Gaussian closely enough for a background wash. Only the raw pixel buffer is
// touched, so the helper is safe to run on a worker thread.
QImage blurImage(const QImage& source, int radius);
