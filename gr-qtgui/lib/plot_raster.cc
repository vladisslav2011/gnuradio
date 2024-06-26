/* -*- c++ -*- */
/*
 * Copyright 2012,2013 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include <gnuradio/qtgui/plot_raster.h>

#include "qwt_color_map.h"
#include "qwt_painter.h"
#include "qwt_scale_map.h"
#include <qimage.h>
#include <qpainter.h>
#include <qpen.h>


typedef QVector<QRgb> QwtColorTable;

class PlotTimeRasterImage : public QImage
{
    // This class hides some Qt3/Qt4 API differences
public:
    PlotTimeRasterImage(const QSize& size, QwtColorMap::Format format)
        : QImage(size,
                 format == QwtColorMap::RGB ? QImage::Format_ARGB32
                                            : QImage::Format_Indexed8)
    {
    }

    PlotTimeRasterImage(const QImage& other) : QImage(other) {}

    void initColorTable(const QImage& other) { setColorTable(other.colorTable()); }
};

class PlotTimeRaster::PrivateData
{
public:
    PrivateData()
    {
        data = NULL;
        colorMap = new QwtLinearColorMap();
    }

    ~PrivateData() { delete colorMap; }

    TimeRasterData* data;
    QwtColorMap* colorMap;
};

/*!
  Sets the following item attributes:
  - QwtPlotItem::AutoScale: true
  - QwtPlotItem::Legend:    false

  The z value is initialized to 8.0.

  \param title Title

  \sa QwtPlotItem::setItemAttribute(), QwtPlotItem::setZ()
*/
PlotTimeRaster::PlotTimeRaster(const QString& title) : QwtPlotRasterItem(title)
{
    d_data = new PrivateData();

    setItemAttribute(QwtPlotItem::AutoScale, true);
    setItemAttribute(QwtPlotItem::Legend, false);

    setZ(1.0);
}

//! Destructor
PlotTimeRaster::~PlotTimeRaster() { delete d_data; }

const TimeRasterData* PlotTimeRaster::data() const { return d_data->data; }

void PlotTimeRaster::setData(TimeRasterData* data) { d_data->data = data; }

//! \return QwtPlotItem::Rtti_PlotSpectrogram
int PlotTimeRaster::rtti() const { return QwtPlotItem::Rtti_PlotGrid; }

/*!
  Change the color map

  Often it is useful to display the mapping between intensities and
  colors as an additional plot axis, showing a color bar.

  \param map Color Map

  \sa colorMap(), QwtScaleWidget::setColorBarEnabled(),
  QwtScaleWidget::setColorMap()
*/
void PlotTimeRaster::setColorMap(const QwtColorMap* map)
{
    delete d_data->colorMap;
    d_data->colorMap = const_cast<QwtColorMap*>(map);

    invalidateCache();
    itemChanged();
}

/*!
  \return Color Map used for mapping the intensity values to colors
  \sa setColorMap()
*/
const QwtColorMap& PlotTimeRaster::colorMap() const { return *d_data->colorMap; }

/*!
  \brief Render an image from the data and color map.

  The area is translated into a rect of the paint device.
  For each pixel of this rect the intensity is mapped
  into a color.

  \param xMap X-Scale Map
  \param yMap Y-Scale Map
  \param area Area that should be rendered in scale coordinates.

  \return A QImage::Format_Indexed8 or QImage::Format_ARGB32 depending
  on the color map.

  \sa QwtRasterData::intensity(), QwtColorMap::rgb(),
  QwtColorMap::colorIndex()
*/
QImage PlotTimeRaster::renderImage(const QwtScaleMap& xMap,
                                   const QwtScaleMap& yMap,
                                   const QRectF& area,
                                   const QSize& size) const
{
    if (area.isEmpty())
        return QImage();

    QRect rect = QwtScaleMap::transform(xMap, yMap, area).toRect();
    const QSize res(-1, -1);

    QwtScaleMap xxMap = xMap;
    QwtScaleMap yyMap = yMap;

    if (res.isValid()) {
        /*
          It is useless to render an image with a higher resolution
          than the data offers. Of course someone will have to
          scale this image later into the size of the given rect, but f.e.
          in case of postscript this will done on the printer.
        */
        rect.setSize(rect.size().boundedTo(res));

        int px1 = rect.x();
        int px2 = rect.x() + rect.width();
        if (xMap.p1() > xMap.p2())
            qSwap(px1, px2);

        double sx1 = area.x();
        double sx2 = area.x() + area.width();
        if (xMap.s1() > xMap.s2())
            qSwap(sx1, sx2);

        int py1 = rect.y();
        int py2 = rect.y() + rect.height();
        if (yMap.p1() > yMap.p2())
            qSwap(py1, py2);

        double sy1 = area.y();
        double sy2 = area.y() + area.height();
        if (yMap.s1() > yMap.s2())
            qSwap(sy1, sy2);

        xxMap.setPaintInterval(px1, px2);
        xxMap.setScaleInterval(sx1, sx2);
        yyMap.setPaintInterval(py1, py2);
        yyMap.setScaleInterval(sy1, sy2);
    }

    PlotTimeRasterImage image(rect.size(), d_data->colorMap->format());

    const QwtInterval intensityRange = d_data->data->interval(Qt::ZAxis);
    if (!intensityRange.isValid())
        return std::move(image);

    d_data->data->initRaster(area, rect.size());

    if (d_data->colorMap->format() == QwtColorMap::RGB) {
        for (int y = rect.top(); y <= rect.bottom(); y++) {
            const double ty = yyMap.invTransform(y);

            QRgb* line = (QRgb*)image.scanLine(y - rect.top());

            for (int x = rect.left(); x <= rect.right(); x++) {
                const double tx = xxMap.invTransform(x);

                *line++ =
                    d_data->colorMap->rgb(intensityRange, d_data->data->value(tx, ty));
            }
        }
        d_data->data->incrementResidual();
    } else if (d_data->colorMap->format() == QwtColorMap::Indexed) {
#if QWT_VERSION >= 0x060200
        image.setColorTable(d_data->colorMap->colorTable(256));
#else
        image.setColorTable(d_data->colorMap->colorTable(intensityRange));
#endif

        for (int y = rect.top(); y <= rect.bottom(); y++) {
            const double ty = yyMap.invTransform(y);

            unsigned char* line = image.scanLine(y - rect.top());
            for (int x = rect.left(); x <= rect.right(); x++) {
                const double tx = xxMap.invTransform(x);

#if QWT_VERSION >= 0x060200
                *line++ = d_data->colorMap->colorIndex(
                    256, intensityRange, d_data->data->value(tx, ty));
#else
                *line++ = d_data->colorMap->colorIndex(intensityRange,
                                                       d_data->data->value(tx, ty));
#endif
            }
        }
    }

    d_data->data->discardRaster();

    // Mirror the image in case of inverted maps

    const bool hInvert = xxMap.p1() > xxMap.p2();
    const bool vInvert = yyMap.p1() > yyMap.p2();
    if (hInvert || vInvert) {
        image = image.mirrored(hInvert, vInvert);
    }

    return std::move(image);
}

QwtInterval PlotTimeRaster::interval(Qt::Axis ax) const
{
    return d_data->data->interval(ax);
}
