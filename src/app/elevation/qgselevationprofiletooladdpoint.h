/***************************************************************************
                          qgselevationprofiletoolidentify.h
                          ---------------
    begin                : April 2022
    copyright            : (C) 2022 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSELEVATIONPROFILETOOLADDPOINT_H
#define QGSELEVATIONPROFILETOOLADDPOINT_H

#include "qgsplottool.h"
#include "qgspoint.h"

class QgsPlotRectangularRubberBand;
class QgsElevationProfileCanvas;
class QgsVectorLayer;

class QgsElevationProfileToolAddPoint : public QgsPlotTool
{

    Q_OBJECT

  public:

    QgsElevationProfileToolAddPoint( QgsElevationProfileCanvas *canvas );
    ~QgsElevationProfileToolAddPoint() override;

    Qgis::PlotToolFlags flags() const override;
    void plotReleaseEvent( QgsPlotMouseEvent *event ) override;

    void setLayer( QgsVectorLayer *layer );

  private slots:

    void toggleAction();
    void onLayerDestroyed( QObject *layer );

  private:



    QgsVectorLayer *mLayer = nullptr;

};

#endif // QGSELEVATIONPROFILETOOLADDPOINT_H
