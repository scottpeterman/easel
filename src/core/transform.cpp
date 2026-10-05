#include "transform.h"

#include "color.h"
#include "tilestore.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace easeletch {

namespace {

constexpr double kEps = 1e-6;

bool nearInteger(double v)
{
    return std::abs(v - std::round(v)) < kEps;
}

// Quarter turns keep pixels on the grid.
bool isQuarterTurn(double angle)
{
    return nearInteger(angle / 90.0);
}

double normalizedAngle(double angle)
{
    double a = std::fmod(angle, 360.0);
    if (a > 180.0)
        a -= 360.0;
    else if (a <= -180.0)
        a += 360.0;
    return a;
}

// The handle's direction from the centre: -1, 0 or 1 on each axis.
QPoint handleDirection(TransformHandle h)
{
    switch (h) {
    case TransformHandle::TopLeft: return {-1, -1};
    case TransformHandle::Top: return {0, -1};
    case TransformHandle::TopRight: return {1, -1};
    case TransformHandle::Right: return {1, 0};
    case TransformHandle::BottomRight: return {1, 1};
    case TransformHandle::Bottom: return {0, 1};
    case TransformHandle::BottomLeft: return {-1, 1};
    case TransformHandle::Left: return {-1, 0};
    default: return {0, 0};
    }
}

QPointF rotated(const QPointF &p, double degrees)
{
    return QTransform().rotate(degrees).map(p);
}

// The pixel rectangle a mapped rectangle covers.
QRect coveredRect(const QRectF &r)
{
    const int x0 = int(std::floor(r.left() + kEps)), y0 = int(std::floor(r.top() + kEps));
    const int x1 = int(std::ceil(r.right() - kEps)), y1 = int(std::ceil(r.bottom() - kEps));
    return QRect(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0));
}

// The same as resample() below, for a matrix with perspective in it: where a
// result pixel comes from is no longer the same sum for every pixel, so each
// one is traced back on its own.
std::vector<float> resampleProjective(const std::vector<float> &src, int w, int h, int channels,
                                      const QTransform &matrix, bool smooth, const QRect &clip, QRect *outRect)
{
    QRect out = coveredRect(matrix.map(QPolygonF(QRectF(0, 0, w, h))).boundingRect());
    if (!clip.isNull()) {
        const QRect kept = out & clip;
        out = kept.isEmpty() ? QRect(out.topLeft(), QSize(1, 1)) : kept;
    }
    *outRect = out;
    std::vector<float> dst(size_t(out.width()) * size_t(out.height()) * size_t(channels), 0.0f);
    bool invertible = false;
    const QTransform inv = matrix.inverted(&invertible);
    if (!invertible)
        return dst;
    const double a = inv.m11(), b = inv.m12(), p13 = inv.m13(), c = inv.m21(), d = inv.m22(), p23 = inv.m23(),
                 e = inv.m31(), f = inv.m32(), p33 = inv.m33();
    // The divisor has one sign everywhere on the picture; past the horizon
    // (where a flat thing tilted that far would vanish) it has the other,
    // and there's nothing there.
    const QPointF middle = matrix.map(QPointF(w / 2.0, h / 2.0));
    const double sign = (p13 * middle.x() + p23 * middle.y() + p33) < 0.0 ? -1.0 : 1.0;
    struct Source {
        double u, v;
        bool ok;
    };
    const auto trace = [&](double X, double Y) {
        const double W = (p13 * X + p23 * Y + p33) * sign;
        if (W <= 1e-9)
            return Source{0.0, 0.0, false};
        return Source{(a * X + c * Y + e) * sign / W, (b * X + d * Y + f) * sign / W, true};
    };
    const auto at = [&](int x, int y) {
        return &src[(size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(std::clamp(x, 0, w - 1))) * size_t(channels)];
    };
    const auto addBilinear = [&](float *px, double u, double v, float share) {
        const double fu = u - 0.5, fv = v - 0.5;
        const int x0 = int(std::floor(fu)), y0 = int(std::floor(fv));
        const float tx = float(fu - x0), ty = float(fv - y0);
        const float k[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
        const float *s[4] = {at(x0, y0), at(x0 + 1, y0), at(x0, y0 + 1), at(x0 + 1, y0 + 1)};
        for (int i = 0; i < 4; ++i)
            for (int ch = 0; ch < channels; ++ch)
                px[ch] += s[i][ch] * k[i] * share;
    };

    // Where each corner of each result pixel comes from: one row of them at a
    // time, each shared by the pixels either side.
    const int cols = out.width() + 1;
    const size_t count = size_t(cols);
    std::vector<Source> above(count), below(count);
    const auto traceRow = [&](std::vector<Source> &row, int y) {
        for (int x = 0; x < cols; ++x)
            row[size_t(x)] = trace(out.left() + x, y);
    };
    traceRow(below, out.top());
    for (int oy = 0; oy < out.height(); ++oy) {
        std::swap(above, below);
        traceRow(below, out.top() + oy + 1);
        float *row = &dst[size_t(oy) * size_t(out.width()) * size_t(channels)];
        for (int ox = 0; ox < out.width(); ++ox) {
            float *px = row + size_t(ox) * size_t(channels);
            const double X = out.left() + ox, Y = out.top() + oy;
            if (!smooth) {
                const Source s = trace(X + 0.5, Y + 0.5);
                if (s.ok && s.u >= 0.0 && s.v >= 0.0 && s.u < w && s.v < h) {
                    const float *from = at(int(std::floor(s.u)), int(std::floor(s.v)));
                    std::copy(from, from + channels, px);
                }
                continue;
            }
            const Source corner[4] = {above[size_t(ox)], above[size_t(ox + 1)], below[size_t(ox)],
                                      below[size_t(ox + 1)]};
            const bool traced = corner[0].ok && corner[1].ok && corner[2].ok && corner[3].ok;
            double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
            if (traced) {
                u0 = std::min({corner[0].u, corner[1].u, corner[2].u, corner[3].u});
                u1 = std::max({corner[0].u, corner[1].u, corner[2].u, corner[3].u});
                v0 = std::min({corner[0].v, corner[1].v, corner[2].v, corner[3].v});
                v1 = std::max({corner[0].v, corner[1].v, corner[2].v, corner[3].v});
                if (u1 <= 0.0 || v1 <= 0.0 || u0 >= w || v0 >= h)
                    continue; // nowhere near the source
                // Well inside it, and no smaller than it was: one reading does.
                if (u0 >= 0.0 && v0 >= 0.0 && u1 <= w && v1 <= h && u1 - u0 <= 1.5 && v1 - v0 <= 1.5) {
                    addBilinear(px, (corner[0].u + corner[1].u + corner[2].u + corner[3].u) / 4.0,
                                (corner[0].v + corner[1].v + corner[2].v + corner[3].v) / 4.0, 1.0f);
                    continue;
                }
            }
            // On the edge, or shrunk: several readings across the pixel, so
            // the edge comes out soft and nothing in the source is skipped.
            const double span = traced ? std::max(u1 - u0, v1 - v0) : 1.0;
            const int n = std::clamp(int(std::ceil(span - 1e-3)), 3, 8);
            const float share = 1.0f / float(n * n);
            for (int sy = 0; sy < n; ++sy)
                for (int sx = 0; sx < n; ++sx) {
                    const Source s = trace(X + (sx + 0.5) / n, Y + (sy + 0.5) / n);
                    if (s.ok && s.u >= 0.0 && s.v >= 0.0 && s.u <= w && s.v <= h)
                        addBilinear(px, s.u, s.v, share);
                }
        }
    }
    return dst;
}

// Resamples a w x h image of float pixels with `channels` values each.
// Outside the source is zero (transparent).
std::vector<float> resample(const std::vector<float> &src, int w, int h, int channels, const QTransform &matrix,
                            bool smooth, const QRect &clip, QRect *outRect)
{
    if (!matrix.isAffine())
        return resampleProjective(src, w, h, channels, matrix, smooth, clip, outRect);
    QRect out = coveredRect(matrix.mapRect(QRectF(0, 0, w, h)));
    if (!clip.isNull()) {
        // Only what can land on the canvas is worked out, so scaling up a
        // lot never builds an image bigger than the canvas.
        const QRect kept = out & clip;
        out = kept.isEmpty() ? QRect(out.topLeft(), QSize(1, 1)) : kept;
    }
    *outRect = out;
    std::vector<float> dst(size_t(out.width()) * size_t(out.height()) * size_t(channels), 0.0f);
    bool invertible = false;
    const QTransform inv = matrix.inverted(&invertible);
    if (!invertible)
        return dst;

    // Flips, quarter turns and whole-pixel moves copy pixels one to one.
    const bool unit = nearInteger(matrix.m11()) && nearInteger(matrix.m12()) && nearInteger(matrix.m21())
                      && nearInteger(matrix.m22()) && nearInteger(matrix.dx()) && nearInteger(matrix.dy())
                      && std::abs(std::abs(matrix.determinant()) - 1.0) < kEps;
    if (unit)
        smooth = false;

    const double a = inv.m11(), b = inv.m12(), c = inv.m21(), d = inv.m22(), e = inv.dx(), f = inv.dy();
    // How many source pixels one result pixel spans each way: when shrinking,
    // that many samples are averaged so nothing is skipped.
    const int nx = smooth ? std::clamp(int(std::ceil(std::hypot(a, b) - 1e-3)), 1, 16) : 1;
    const int ny = smooth ? std::clamp(int(std::ceil(std::hypot(c, d) - 1e-3)), 1, 16) : 1;
    const float weight = 1.0f / float(nx * ny);

    const auto at = [&](int x, int y) -> const float * {
        return (x < 0 || y < 0 || x >= w || y >= h) ? nullptr : &src[(size_t(y) * size_t(w) + size_t(x)) * size_t(channels)];
    };
    // Smooth sampling reads the source with its edge pixels repeated outward,
    // so a hard-edged block scaled up stays solid right to its edge instead
    // of fading into the transparency around it. The edge of the box itself
    // is antialiased separately: each sample counts by how far inside the
    // source it is, measured in result pixels (kx, ky: result pixels per
    // source pixel, over the width of one sample's footprint in that direction).
    const auto clamped = [&](int x, int y) -> const float * {
        return &src[(size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(std::clamp(x, 0, w - 1))) * size_t(channels)];
    };
    const double lenX = std::hypot(matrix.m11(), matrix.m12()), lenY = std::hypot(matrix.m21(), matrix.m22());
    const auto edgeScale = [&](double mx, double my, double len) {
        if (len <= 0.0)
            return 0.0;
        const double footprint = std::abs(mx / len) / nx + std::abs(my / len) / ny;
        return len / std::max(footprint, 1e-9);
    };
    const double kx = edgeScale(matrix.m11(), matrix.m12(), lenX), ky = edgeScale(matrix.m21(), matrix.m22(), lenY);

    for (int oy = 0; oy < out.height(); ++oy) {
        float *row = &dst[size_t(oy) * size_t(out.width()) * size_t(channels)];
        for (int ox = 0; ox < out.width(); ++ox) {
            float *px = row + size_t(ox) * size_t(channels);
            if (!smooth) {
                const double X = out.left() + ox + 0.5, Y = out.top() + oy + 0.5;
                const double u = a * X + c * Y + e, v = b * X + d * Y + f;
                if (const float *s = at(int(std::floor(u)), int(std::floor(v))))
                    std::copy(s, s + channels, px);
                continue;
            }
            for (int sy = 0; sy < ny; ++sy) {
                for (int sx = 0; sx < nx; ++sx) {
                    const double X = out.left() + ox + (sx + 0.5) / nx, Y = out.top() + oy + (sy + 0.5) / ny;
                    const double u = a * X + c * Y + e, v = b * X + d * Y + f;
                    const double cover = std::clamp(std::min(u, w - u) * kx + 0.5, 0.0, 1.0)
                                         * std::clamp(std::min(v, h - v) * ky + 0.5, 0.0, 1.0);
                    if (cover <= 0.0)
                        continue;
                    const double fu = u - 0.5, fv = v - 0.5;
                    const int x0 = int(std::floor(fu)), y0 = int(std::floor(fv));
                    const float tx = float(fu - x0), ty = float(fv - y0);
                    const float k[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                    const float *p[4] = {clamped(x0, y0), clamped(x0 + 1, y0), clamped(x0, y0 + 1),
                                         clamped(x0 + 1, y0 + 1)};
                    const float share = float(cover) * weight;
                    for (int i = 0; i < 4; ++i) {
                        if (k[i] <= 0.0f)
                            continue;
                        for (int ch = 0; ch < channels; ++ch)
                            px[ch] += p[i][ch] * k[i] * share;
                    }
                }
            }
        }
    }
    return dst;
}

} // namespace

QTransform FreeTransform::matrix() const
{
    QTransform m;
    m.translate(center.x(), center.y());
    m.rotate(angle);
    m.scale(scaleX, scaleY);
    m.translate(-size.width() / 2.0, -size.height() / 2.0);
    if (isQuarterTurn(angle)) {
        // Keep the result on the pixel grid: a 3 x 4 block turned a quarter
        // about its centre would otherwise sit on half pixels and blur.
        const QRectF r = m.mapRect(QRectF(QPointF(0, 0), size));
        m *= QTransform::fromTranslate(std::round(r.left()) - r.left(), std::round(r.top()) - r.top());
    }
    return m;
}

QPolygonF FreeTransform::corners() const
{
    return matrix().map(QPolygonF(QRectF(QPointF(0, 0), size))).mid(0, 4);
}

bool FreeTransform::isIdentity() const
{
    return std::abs(scaleX - 1.0) < 1e-9 && std::abs(scaleY - 1.0) < 1e-9 && std::abs(normalizedAngle(angle)) < 1e-9
           && nearInteger(center.x() - size.width() / 2.0) && nearInteger(center.y() - size.height() / 2.0);
}

void FreeTransform::flipHorizontal()
{
    scaleX = -scaleX;
    angle = normalizedAngle(-angle);
}

void FreeTransform::flipVertical()
{
    scaleY = -scaleY;
    angle = normalizedAngle(-angle);
}

void FreeTransform::rotate(double degrees)
{
    angle = normalizedAngle(angle + degrees);
}

QPointF handlePosition(const FreeTransform &t, TransformHandle handle)
{
    const QPoint dir = handleDirection(handle);
    const QPointF local(dir.x() * t.size.width() * t.scaleX / 2.0, dir.y() * t.size.height() * t.scaleY / 2.0);
    return t.center + rotated(local, t.angle);
}

TransformHandle hitTest(const FreeTransform &t, const QPointF &pos, double tolerance)
{
    const QPointF local = rotated(pos - t.center, -t.angle);
    const double hw = std::abs(t.size.width() * t.scaleX) / 2.0, hh = std::abs(t.size.height() * t.scaleY) / 2.0;
    const bool inside = std::abs(local.x()) <= hw && std::abs(local.y()) <= hh;
    // A box too small to hold all its handles has only the corner ones, and
    // its middle always moves it, however much the handles overlap there.
    const bool small = hw * 2.0 < tolerance * 4.0 || hh * 2.0 < tolerance * 4.0;
    if (small && std::abs(local.x()) <= hw / 2.0 && std::abs(local.y()) <= hh / 2.0)
        return TransformHandle::Move;

    const auto close = [&](TransformHandle h) { return QLineF(handlePosition(t, h), pos).length() <= tolerance; };
    for (const TransformHandle h : {TransformHandle::TopLeft, TransformHandle::TopRight, TransformHandle::BottomRight,
                                    TransformHandle::BottomLeft})
        if (close(h))
            return h;
    if (!small) {
        for (const TransformHandle h : {TransformHandle::Top, TransformHandle::Right, TransformHandle::Bottom,
                                        TransformHandle::Left})
            if (close(h))
                return h;
    }
    return inside ? TransformHandle::Move : TransformHandle::Rotate;
}

FreeTransform dragHandle(const FreeTransform &start, TransformHandle handle, const QPointF &from, const QPointF &to,
                         bool constrain)
{
    FreeTransform t = start;
    if (handle == TransformHandle::None)
        return t;

    if (handle == TransformHandle::Move) {
        QPointF d(std::round(to.x() - from.x()), std::round(to.y() - from.y()));
        if (constrain)
            (std::abs(d.x()) >= std::abs(d.y()) ? d.ry() : d.rx()) = 0.0;
        t.center = start.center + d;
        return t;
    }

    if (handle == TransformHandle::Rotate) {
        const QPointF a = from - start.center, b = to - start.center;
        if ((a.x() == 0.0 && a.y() == 0.0) || (b.x() == 0.0 && b.y() == 0.0))
            return t;
        constexpr double kDeg = 180.0 / 3.14159265358979323846;
        double angle = start.angle + (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * kDeg;
        if (constrain)
            angle = std::round(angle / 15.0) * 15.0;
        else if (std::abs(angle - std::round(angle / 90.0) * 90.0) < 0.05)
            angle = std::round(angle / 90.0) * 90.0; // back onto the grid when it's this close
        t.angle = normalizedAngle(angle);
        return t;
    }

    // Scaling, worked out in the box's own (unrotated) frame.
    const QPoint dir = handleDirection(handle);
    const QPointF delta = rotated(to - from, -start.angle);
    const double w0 = start.size.width() * start.scaleX, h0 = start.size.height() * start.scaleY; // signed
    double w = w0 + delta.x() * dir.x(), h = h0 + delta.y() * dir.y();
    const bool corner = dir.x() != 0 && dir.y() != 0;
    if (corner && !constrain && w0 != 0.0 && h0 != 0.0) {
        // Proportional: the axis dragged further decides.
        const double fx = w / w0, fy = h / h0;
        const double f = std::abs(fx - 1.0) >= std::abs(fy - 1.0) ? fx : fy;
        w = w0 * f;
        h = h0 * f;
    }
    const bool grid = isQuarterTurn(start.angle);
    const auto settle = [grid](double v, double was) {
        if (grid)
            v = std::round(v);
        const double least = grid ? 1.0 : 0.01;
        if (std::abs(v) < least)
            v = (v < 0.0 || (v == 0.0 && was < 0.0)) ? -least : least;
        return v;
    };
    w = settle(w, w0);
    h = settle(h, h0);

    // The opposite handle stays put: the centre moves half as far as the edge.
    const QPointF shift(dir.x() * (w - w0) / 2.0, dir.y() * (h - h0) / 2.0);
    t.center = start.center + rotated(shift, start.angle);
    if (start.size.width() > 0.0)
        t.scaleX = w / start.size.width();
    if (start.size.height() > 0.0)
        t.scaleY = h / start.size.height();
    return t;
}

bool isWarpable(const QPolygonF &quad)
{
    if (quad.size() != 4)
        return false;
    // Every corner turns the same way, and by enough to be a corner: neither
    // folded, dented, nor squashed flat.
    double area = 0.0, longest = 0.0;
    int turns = 0;
    for (int i = 0; i < 4; ++i) {
        const QPointF p = quad.at(i), q = quad.at((i + 1) % 4), r = quad.at((i + 2) % 4);
        const double cross = (q.x() - p.x()) * (r.y() - q.y()) - (q.y() - p.y()) * (r.x() - q.x());
        if (std::abs(cross) < 1.0)
            return false;
        turns += cross > 0.0 ? 1 : -1;
        area += p.x() * q.y() - q.x() * p.y();
        longest = std::max(longest, std::hypot(q.x() - p.x(), q.y() - p.y()));
    }
    // At least a pixel thick, measured across its longest side.
    area = std::abs(area) / 2.0;
    return std::abs(turns) == 4 && area >= 4.0 && area / longest >= 1.0;
}

QTransform warpMatrix(const QSizeF &size, const QPolygonF &quad, bool *ok)
{
    QTransform matrix;
    const bool good = size.width() > 0.0 && size.height() > 0.0 && isWarpable(quad)
                      && QTransform::quadToQuad(QPolygonF(QRectF(QPointF(0, 0), size)).mid(0, 4), quad, matrix);
    if (ok)
        *ok = good;
    return good ? matrix : QTransform();
}

QImage transformImage(const QImage &source, const QTransform &matrix, bool smooth, QPoint *origin,
                      const QRect &clip)
{
    if (source.isNull() || source.format() != TileStore::TileFormat) {
        if (origin)
            *origin = QPoint();
        return QImage();
    }
    const int w = source.width(), h = source.height();
    std::vector<float> src(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        const auto *s = reinterpret_cast<const Pixel *>(source.constScanLine(y));
        float *d = &src[size_t(y) * size_t(w) * 4];
        for (int x = 0; x < w; ++x, d += 4) {
            d[0] = float(s[x].r);
            d[1] = float(s[x].g);
            d[2] = float(s[x].b);
            d[3] = float(s[x].a);
        }
    }
    QRect rect;
    const std::vector<float> dst = resample(src, w, h, 4, matrix, smooth, clip, &rect);
    QImage out(rect.size(), TileStore::TileFormat);
    for (int y = 0; y < rect.height(); ++y) {
        auto *d = reinterpret_cast<Pixel *>(out.scanLine(y));
        const float *s = &dst[size_t(y) * size_t(rect.width()) * 4];
        for (int x = 0; x < rect.width(); ++x, s += 4)
            d[x] = makePixel(s[0], s[1], s[2], s[3]);
    }
    if (origin)
        *origin = rect.topLeft();
    return out;
}

Selection transformShape(const Selection &shape, const QSize &sourceSize, const QTransform &matrix,
                         const QRect &clip)
{
    const int w = sourceSize.width(), h = sourceSize.height();
    if (w <= 0 || h <= 0)
        return {};
    const QRect whole(0, 0, w, h);
    const bool isWhole = shape.isEmpty() || (shape.shape() == Selection::Shape::Rect && shape.bounds() == whole);
    std::vector<float> src(size_t(w) * size_t(h), isWhole ? 1.0f : 0.0f);
    if (!isWhole) {
        const QRect b = shape.bounds() & whole;
        for (int y = b.top(); y <= b.bottom(); ++y)
            for (int x = b.left(); x <= b.right(); ++x)
                src[size_t(y) * size_t(w) + size_t(x)] = shape.contains(x, y) ? 1.0f : 0.0f;
    }
    QRect rect;
    const std::vector<float> dst = resample(src, w, h, 1, matrix, false, clip, &rect);
    QImage mask(rect.size(), QImage::Format_Grayscale8);
    bool full = true;
    for (int y = 0; y < rect.height(); ++y) {
        uchar *m = mask.scanLine(y);
        const float *s = &dst[size_t(y) * size_t(rect.width())];
        for (int x = 0; x < rect.width(); ++x) {
            m[x] = s[x] >= 0.5f ? 255 : 0;
            full = full && m[x];
        }
    }
    const QRect local(QPoint(0, 0), rect.size());
    return full ? Selection::rect(local) : Selection::mask(local, mask);
}

} // namespace easeletch
