/***************************************************************************
                          qgselevationprofiletoolmovepoint.cpp
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

#include "qgselevationprofiletoolmovepoint.h"
#include "qgsabstractprofilegenerator.h"
#include "qgselevationprofilecanvas.h"
#include "qgsfeatureid.h"
#include "qgsplotmouseevent.h"
#include "qgsplotrubberband.h"
#include "qgspointxy.h"
#include <qaction.h>
#include <qnamespace.h>


QgsElevationProfileToolMovePoint::QgsElevationProfileToolMovePoint( QgsElevationProfileCanvas *canvas )
  : QgsPlotTool( canvas, tr( "Move Point Feature" ) )
{
  setCursor( Qt::CrossCursor );

  mRubberBand.reset( new QgsPlotPointRubberBand( canvas ) );
  mRubberBand->setBrush( QBrush( QColor( 254, 178, 76, 63 ) ) );
  mRubberBand->setPen( QPen( QBrush( QColor( 254, 58, 29, 100 ) ), 0 ) );
}

QgsElevationProfileToolMovePoint::~QgsElevationProfileToolMovePoint() = default;

void QgsElevationProfileToolMovePoint::plotReleaseEvent( QgsPlotMouseEvent *event )
{
  event->ignore();
  if ( event->button() != Qt::LeftButton || !mLayer )
    return;

  QgsPointXY snappedPoint = event->snappedPoint();

  if ( mDragging )
  {
    mRubberBand->finish( QPointF(), Qt::KeyboardModifiers() );
    QgsGeometry geometry( std::make_unique<QgsPoint>( toMapCoordinates( snappedPoint ) ) );
    mLayer->changeGeometry( mFeatureId, geometry );
  }
  else
  {
    if ( !findFeature( snappedPoint.toQPointF() ) )
      return;

    mRubberBand->start( snappedPoint.toQPointF(), Qt::KeyboardModifiers() );
  }
  mDragging = !mDragging;
}

void QgsElevationProfileToolMovePoint::plotMoveEvent( QgsPlotMouseEvent *event )
{
  event->ignore();
  if ( !mDragging || !mRubberBand )
    return;

  mRubberBand->update( event->snappedPoint().toQPointF(), Qt::KeyboardModifiers() );
}

bool QgsElevationProfileToolMovePoint::findFeature( QPointF pos )
{
  QVector<QgsProfileIdentifyResults> results = qgis::down_cast< QgsElevationProfileCanvas * >( mCanvas )->identify( pos );
  for ( const auto &result : results )
  {
    if ( result.layer() != mLayer )
      continue;

    for ( const auto &featureParam : result.results() )
    {
      if ( featureParam.contains( "id" ) )
      {
        mFeatureId = featureParam.value( "id" ).toLongLong();
        return true;  // return as soon as a feature is found
      }
    }
  }
  return false;
}

void QgsElevationProfileToolMovePoint::setLayer( QgsVectorLayer *layer )
{
  if ( layer == mLayer )
  {
    return;
  }

  if ( mLayer )
  {
    disconnect( mLayer, &QgsVectorLayer::editingStarted, this, &QgsElevationProfileToolMovePoint::toggleAction );
    disconnect( mLayer, &QgsVectorLayer::editingStopped, this, &QgsElevationProfileToolMovePoint::toggleAction );
  }

  mLayer = layer;

  if ( mLayer )
  {
    connect( mLayer, &QgsVectorLayer::editingStarted, this, &QgsElevationProfileToolMovePoint::toggleAction );
    connect( mLayer, &QgsVectorLayer::editingStopped, this, &QgsElevationProfileToolMovePoint::toggleAction );
  }

  toggleAction();
}

void QgsElevationProfileToolMovePoint::toggleAction()
{
  QAction *movePointAction = action();
  if ( !movePointAction )
    return;

  if ( mLayer )
  {
    const bool isPoint = mLayer->geometryType() == Qgis::GeometryType::Point;
    const bool canMoveFeatures = mLayer->dataProvider()->capabilities() & Qgis::VectorProviderCapability::ChangeGeometries;
    movePointAction->setEnabled( isPoint && canMoveFeatures && mLayer->isEditable() );
  }
  else
  {
    movePointAction->setEnabled( false );
  }
}
