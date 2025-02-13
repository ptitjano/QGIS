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
#include "moc_qgselevationprofiletoolmovepoint.cpp"
#include "qgsabstractprofilegenerator.h"
#include "qgselevationprofilecanvas.h"
#include "qgsmessagebar.h"
#include "qgsplotmouseevent.h"
#include "qgsplotrubberband.h"
#include "qgisapp.h"
#include "qgspointxy.h"
#include "qgsproject.h"
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

void QgsElevationProfileToolMovePoint::keyPressEvent( QKeyEvent *e )
{
  if ( !e->isAutoRepeat() )
  {
    switch ( e->key() )
    {
      case Qt::Key_Control:
        mAbscissaLocked = true;
        QgisApp::instance()->statusBar()->showMessage( tr( "Abscissa locked" ) );
        break;
      default:
        break;
    }
  }
}

void QgsElevationProfileToolMovePoint::keyReleaseEvent( QKeyEvent * /* e */ )
{
  QgisApp::instance()->statusBar()->showMessage( tr( "Hold Ctrl key to lock the abscissa" ) );
  qgis::down_cast<QgsElevationProfileCanvas *>( mCanvas )->setCrossHairsItemIsDelegate( false );
  mAbscissaLocked = false;
}

void QgsElevationProfileToolMovePoint::plotReleaseEvent( QgsPlotMouseEvent *event )
{
  event->ignore();
  if ( event->button() != Qt::LeftButton || !mLayer )
    return;

  QgsPointXY snappedPoint = event->snappedPoint();

  if ( mDragging )
  {
    QgsPointXY snappedPoint;
    if ( mAbscissaLocked )
    {
      QgsPointXY point = event->pos();
      point.setX( mStartX );
      snappedPoint = mCanvas->snapToPlot( point.toQPointF().toPoint() );
      if ( snappedPoint.isEmpty() )
        snappedPoint = point;
    }
    else
      snappedPoint = event->snappedPoint();

    QgsGeometry geometry( std::make_unique<QgsPoint>( toMapCoordinates( snappedPoint ) ) );

    if ( geometry.isNull() || geometry.isEmpty() )
    {
      QgisApp::instance()->messageBar()->pushWarning( tr( "Add point" ), tr( "Could not add point: no profile curve" ) );
      QgsDebugError( QStringLiteral( "Could not add point with no geometry" ) );
      return;
    }
    QgsCoordinateTransform transform( mCanvas->crs(), mLayer->crs3D(), QgsProject::instance()->transformContext() );
    transform.setBallparkTransformsAreAppropriate( true );
    try
    {
      geometry.transform( transform, Qgis::TransformDirection::Forward, true );
    }
    catch ( QgsCsException &cse )
    {
      Q_UNUSED( cse )
      QgsDebugError( QStringLiteral( "Caught CRS exception %1" ).arg( cse.what() ) );
      return;
    }

    mLayer->changeGeometry( mFeatureId, geometry );
    mRubberBand->finish( QPointF(), Qt::KeyboardModifiers() );
  }
  else
  {
    if ( !findFeature( snappedPoint.toQPointF() ) )
      return;

    mStartX = snappedPoint.toQPointF().x();
    mRubberBand->start( snappedPoint.toQPointF(), Qt::KeyboardModifiers() );
  }
  mDragging = !mDragging;
}

void QgsElevationProfileToolMovePoint::plotMoveEvent( QgsPlotMouseEvent *event )
{
  event->ignore();
  if ( !mDragging || !mRubberBand )
    return;

  QgsElevationProfileCanvas *profileCanvas = qgis::down_cast<QgsElevationProfileCanvas *>( mCanvas );
  profileCanvas->setCrossHairsItemIsDelegate( mAbscissaLocked );

  QgsPointXY snappedPoint;
  if ( mAbscissaLocked )
  {
    QgsPointXY point = event->pos();
    point.setX( mStartX );
    snappedPoint = mCanvas->snapToPlot( point.toQPointF().toPoint() );
    if ( snappedPoint.isEmpty() )
      snappedPoint = point;

    profileCanvas->showCrossHairsItem();
    profileCanvas->setCrossHairsItemPoint( snappedPoint.toQPointF().toPoint() );
  }
  else
    snappedPoint = event->snappedPoint();

  mRubberBand->update( snappedPoint.toQPointF(), Qt::KeyboardModifiers() );
}

bool QgsElevationProfileToolMovePoint::findFeature( QPointF pos )
{
  QVector<QgsProfileIdentifyResults> results = qgis::down_cast<QgsElevationProfileCanvas *>( mCanvas )->identify( pos );
  for ( const auto &result : results )
  {
    if ( result.layer() != mLayer )
      continue;

    for ( const auto &featureParam : result.results() )
    {
      if ( featureParam.contains( "id" ) )
      {
        mFeatureId = featureParam.value( "id" ).toLongLong();
        return true; // return as soon as a feature is found
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

void QgsElevationProfileToolMovePoint::deactivate()
{
  QgisApp::instance()->statusBar()->clearMessage();
  QgsPlotTool::deactivate();
}

void QgsElevationProfileToolMovePoint::activate()
{
  QgisApp::instance()->statusBar()->showMessage( tr( "Hold Ctrl key to lock the abscissa" ) );
  QgsPlotTool::activate();
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
