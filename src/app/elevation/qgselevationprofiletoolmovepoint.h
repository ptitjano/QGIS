/***************************************************************************
                          qgselevationprofiletoolmovepoint.h
                          ---------------
    begin                : December 2024
    copyright            : (C) 2024 by Jacky Volpes
    email                : jacky dot volpes at oslandia dot com
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGSELEVATIONPROFILETOOLMOVEPOINT_H
#define QGSELEVATIONPROFILETOOLMOVEPOINT_H

#include "qgsfeatureid.h"
#include "qgsplottool.h"

class QgsVectorLayer;
class QgsElevationProfileCanvas;
class QgsPlotPointRubberBand;

class QgsElevationProfileToolMovePoint : public QgsPlotTool
{
    Q_OBJECT

  public:
    QgsElevationProfileToolMovePoint( QgsElevationProfileCanvas *canvas );

    ~QgsElevationProfileToolMovePoint() override;

    void plotMoveEvent( QgsPlotMouseEvent *event ) override;
    void plotReleaseEvent( QgsPlotMouseEvent *event ) override;
    void keyPressEvent( QKeyEvent *event ) override;
    void keyReleaseEvent( QKeyEvent *e ) override;
    void activate() override;
    void deactivate() override;

    void setLayer( QgsVectorLayer *layer );

  private slots:
    void toggleAction();

  private:
    void updateRubberBand();
    bool findFeature( QPointF pos );

    QgsElevationProfileCanvas *mElevationCanvas = nullptr;
    QgsVectorLayer *mLayer = nullptr;
    QgsFeatureId mFeatureId;

    std::unique_ptr< QgsPlotPointRubberBand > mRubberBand;
    bool mDragging = false;
    bool mAbscissaLocked = false;
    double mStartX;
};

#endif // QGSELEVATIONPROFILETOOLMOVEPOINT_H
