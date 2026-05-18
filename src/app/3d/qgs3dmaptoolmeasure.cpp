/***************************************************************************
  qgs3dmaptoolmeasure.cpp
  --------------------------------------
  Date                 : Jun 2019
  Copyright            : (C) 2019 by Ismail Sunni
  Email                : imajimatika at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgs3dmaptoolmeasure.h"

#include <memory>

#include "qgs3dmapcanvaswidget.h"
#include "qgs3dmapscene.h"
#include "qgs3dmeasuredialog.h"
#include "qgs3dsnappingmanager.h"
#include "qgs3dutils.h"
#include "qgsabstractterrainsettings.h"
#include "qgscameracontroller.h"
#include "qgsframegraph.h"
#include "qgsmaplayer.h"
#include "qgspoint.h"
#include "qgsrubberband3d.h"
#include "qgssettingsentryimpl.h"
#include "qgssettingsregistrygui.h"
#include "qgswindow3dengine.h"

#include <QKeyEvent>
#include <QString>

#include "moc_qgs3dmaptoolmeasure.cpp"

using namespace Qt::StringLiterals;

Qgs3DMapToolMeasure::Qgs3DMapToolMeasure( Qgs3DMapCanvasWidget *canvasWidget, bool measureArea, Qgs3DSnappingManager *snapper )
  : Qgs3DMapTool( canvasWidget->mapCanvas3D() )
  , mMeasureArea( measureArea )
  , mSnapper( snapper )
{
  // Dialog
  mDialog.reset( new Qgs3DMeasureDialog( this, canvasWidget ) );
  mDialog->setWindowFlags( mDialog->windowFlags() | Qt::Tool );
  mDialog->restorePosition();
}

Qgs3DMapToolMeasure::~Qgs3DMapToolMeasure() = default;

void Qgs3DMapToolMeasure::activate()
{
  const Qgis::GeometryType rubberbandType = mMeasureArea ? Qgis::GeometryType::Polygon : Qgis::GeometryType::Line;
  mRubberBand = std::make_unique<QgsRubberBand3D>( *mCanvas->mapSettings(), mCanvas->engine(), mCanvas->engine()->frameGraph()->rubberBandsRootEntity(), rubberbandType );

  mSnapper->start( mCanvas );

  restart();
  updateSettings();

  // Show dialog
  mDialog->updateSettings();
  mDialog->show();
}

void Qgs3DMapToolMeasure::deactivate()
{
  mRubberBand.reset();

  mSnapper->finish();

  // Hide dialog
  mDialog->hide();
}

QCursor Qgs3DMapToolMeasure::cursor() const
{
  return Qt::CrossCursor;
}

void Qgs3DMapToolMeasure::handleClick( const QPoint &screenPos )
{
  if ( mDone )
  {
    restart();
  }

  bool snapSuccess;
  const QgsPoint snapPoint = mSnapper->screenToMap( screenPos, &snapSuccess );
  if ( !snapSuccess )
  {
    return;
  }

  addPoint( snapPoint );
  mDialog->show();
}

void Qgs3DMapToolMeasure::updateSettings()
{
  if ( mRubberBand )
  {
    mRubberBand->setWidth( 3 );
    mRubberBand->setColor( QgsSettingsRegistryGui::settingsDefaultMeasureColor->value() );
  }
}

void Qgs3DMapToolMeasure::addPoint( const QgsPoint &point )
{
  // don't add points with the same coordinates
  if ( !mPoints.isEmpty() && mPoints.last() == point )
  {
    return;
  }

  const QgsPoint addedPoint( point );

  mPoints.append( addedPoint );
  mDialog->addPoint();

  const QgsPoint newPoint( point.x(), point.y(), point.z() / canvas()->mapSettings()->terrainSettings()->verticalScale() );
  if ( mPoints.size() == 1 )
  {
    mRubberBand->addPoint( newPoint );
    zMean = static_cast<float>( newPoint.z() );
  }
  else
  {
    mRubberBand->moveLastPoint( newPoint );
    zMean += ( static_cast<float>( newPoint.z() ) - zMean ) / static_cast<float>( mPoints.size() );
  }
  mRubberBand->addPoint( newPoint );
}

void Qgs3DMapToolMeasure::restart()
{
  mPoints.clear();
  zMean = std::numeric_limits<float>::quiet_NaN();
  mDone = false;
  mDialog->resetFields();

  if ( mRubberBand )
  {
    mRubberBand->reset();
    mRubberBand->setHideLastMarker( true );
  }

  mSnapper->reset();
}

void Qgs3DMapToolMeasure::undo()
{
  if ( mPoints.empty() )
  {
    return;
  }
  if ( mPoints.size() == 1 )
  {
    //removing first point, so restart everything
    restart();
  }
  else
  {
    mPoints.removeLast();
    mDialog->removeLastPoint();

    mRubberBand->removePenultimatePoint();
  }
}

QVector<QgsPoint> Qgs3DMapToolMeasure::points() const
{
  return mPoints;
}

void Qgs3DMapToolMeasure::mousePressEvent( QMouseEvent *event )
{
  mMouseHasMoved = false;
  mMouseClickPos = event->pos();
}

void Qgs3DMapToolMeasure::mouseMoveEvent( QMouseEvent *event )
{
  if ( !mMouseHasMoved && ( event->pos() - mMouseClickPos ).manhattanLength() >= QApplication::startDragDistance() )
  {
    mMouseHasMoved = true;
  }

  bool snapSuccess;
  const QgsPoint snapPoint = mSnapper->screenToMap( event->pos(), &snapSuccess );

  if ( !snapSuccess || mPoints.isEmpty() || mDone )
  {
    return;
  }

  mRubberBand->moveLastPoint( snapPoint );
}

void Qgs3DMapToolMeasure::mouseReleaseEvent( QMouseEvent *event )
{
  if ( event->button() == Qt::LeftButton && !mMouseHasMoved )
  {
    handleClick( event->pos() );
  }
  else if ( event->button() == Qt::RightButton && !mMouseHasMoved )
  {
    if ( mDone || mPoints.size() <= 1 )
    {
      restart();
      return;
    }

    // Finish measurement
    mRubberBand->setHideLastMarker( false );
    mRubberBand->removeLastPoint();
    mDone = true;
  }
}

void Qgs3DMapToolMeasure::keyPressEvent( QKeyEvent *event )
{
  if ( !mDone && ( event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete ) )
  {
    undo();
  }
  else if ( event->key() == Qt::Key_Escape )
  {
    restart();
  }
}
