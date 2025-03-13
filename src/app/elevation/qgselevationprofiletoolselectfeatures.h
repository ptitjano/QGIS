/***************************************************************************
                          qgselevationprofiletoolselectfeatures.h
                          ---------------
    begin                : January 2025
    copyright            : (C) 2025 by Jacky Volpes
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

#ifndef QGSELEVATIONPROFILETOOLSELECTFEATURES_H
#define QGSELEVATIONPROFILETOOLSELECTFEATURES_H

#include "qgsplottool.h"

class QgsVectorLayer;
class QgsElevationProfileCanvas;
class QgsPlotRectangularRubberBand;

class QgsElevationProfileToolSelectFeatures : public QgsPlotTool
{
    Q_OBJECT

  public:
    QgsElevationProfileToolSelectFeatures( QgsElevationProfileCanvas *canvas );

    ~QgsElevationProfileToolSelectFeatures() override;

    void plotMoveEvent( QgsPlotMouseEvent *event ) override;
    void plotReleaseEvent( QgsPlotMouseEvent *event ) override;
    void plotPressEvent( QgsPlotMouseEvent *event ) override;
    void keyPressEvent( QKeyEvent *event ) override;
    void keyReleaseEvent( QKeyEvent *e ) override;

    void setLayer( QgsVectorLayer *layer );

  private slots:
    void toggleAction();

  private:
    void updateRubberBand();
    bool findFeature( QPointF pos );
    bool findFeatures( QRectF rect );

    QgsElevationProfileCanvas *mElevationCanvas = nullptr;
    QgsVectorLayer *mLayer = nullptr;

    std::unique_ptr<QgsPlotRectangularRubberBand> mRubberBand;
    bool mDragging = false;
    QPointF mMousePressStartPos;

    enum ModifierMode
    {
      NoModifier,
      AddToSelection,
      RemoveFromSelection
    };

    ModifierMode mCurrentModifier = NoModifier;
};

#endif // QGSELEVATIONPROFILETOOLSELECTFEATURES_H
