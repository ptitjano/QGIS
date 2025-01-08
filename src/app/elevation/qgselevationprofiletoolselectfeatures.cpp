/***************************************************************************
                          qgselevationprofiletoolselectfeatures.cpp
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

#include "qgselevationprofiletoolselectfeatures.h"
#include "qgsabstractprofilegenerator.h"
#include "qgselevationprofilecanvas.h"
#include "qgsmessagebar.h"
#include "qgsplotmouseevent.h"
#include "qgsplotrubberband.h"
#include "qgisapp.h"
#include "qgspointxy.h"
#include "qgsproject.h"
#include "qgsapplication.h"
#include <qaction.h>
#include <qnamespace.h>


QgsElevationProfileToolSelectFeatures::QgsElevationProfileToolSelectFeatures( QgsElevationProfileCanvas *canvas )
  : QgsPlotTool( canvas, tr( "Select Features" ) )
{
  setCursor( QgsApplication::getThemeCursor( QgsApplication::Cursor::Select ) );

  mRubberBand.reset( new QgsPlotRectangularRubberBand( canvas ) );
  mRubberBand->setBrush( QBrush( QColor( 254, 178, 76, 63 ) ) );
  mRubberBand->setPen( QPen( QBrush( QColor( 254, 58, 29, 100 ) ), 0 ) );
}

QgsElevationProfileToolSelectFeatures::~QgsElevationProfileToolSelectFeatures() = default;

void QgsElevationProfileToolSelectFeatures::plotPressEvent( QgsPlotMouseEvent *event )
{
  event->ignore();
  if ( event->button() != Qt::LeftButton || !mLayer )
    return;

  mMousePressStartPos = event->pos();
  mRubberBand->start( mMousePressStartPos, Qt::KeyboardModifiers() );
}

void QgsElevationProfileToolSelectFeatures::keyPressEvent( QKeyEvent *e )
{
  if ( !e->isAutoRepeat() )
  {
    switch ( e->key() )
    {
      case Qt::Key_Shift:
        QgisApp::instance()->statusBar()->showMessage( tr( "Add to the current selection" ) );
        mCurrentModifier = AddToSelection;
        break;
      case Qt::Key_Control:
        QgisApp::instance()->statusBar()->showMessage( tr( "Subtract from the current selection" ) );
        mCurrentModifier = RemoveFromSelection;
        break;

      default:
        break;
    }
  }
}

void QgsElevationProfileToolSelectFeatures::keyReleaseEvent( QKeyEvent * /* e */ )
{
  mCurrentModifier = NoModifier;
  QgisApp::instance()->statusBar()->clearMessage();
}

void QgsElevationProfileToolSelectFeatures::plotReleaseEvent( QgsPlotMouseEvent *event )
{
  event->ignore();
  if ( event->button() != Qt::LeftButton || !mLayer )
    return;

  const bool clickOnly = !isClickAndDrag( mMousePressStartPos.toPoint(), event->pos() );
  mRubberBand->finish( event->pos() );
  QVector<QgsProfileIdentifyResults> results;
  if ( !clickOnly )
  {
    // don't allow the band to be dragged outside of the plot area
    const QRectF plotArea = qgis::down_cast<QgsElevationProfileCanvas *>( mCanvas )->plotArea();
    QPointF end;
    if ( !plotArea.contains( event->pos() ) )
    {
      end = constrainPointToRect( event->pos(), plotArea );
    }
    else
    {
      end = event->snappedPoint().toQPointF();
    }

    results = qgis::down_cast<QgsElevationProfileCanvas *>( mCanvas )->identify( QRectF( mMousePressStartPos, end ) );
  }
  else
  {
    results = qgis::down_cast<QgsElevationProfileCanvas *>( mCanvas )->identify( mMousePressStartPos );
  }

  if ( mCurrentModifier == NoModifier && results.empty() )
  {
    mLayer->removeSelection();
    return;
  }

  QgsFeatureIds featureIds;
  for ( const auto &result : results )
  {
    if ( result.layer() != mLayer )
      continue;

    for ( const auto &featureParam : result.results() )
      if ( featureParam.contains( "id" ) )
        featureIds << featureParam.value( "id" ).toLongLong();
  }

  switch ( mCurrentModifier )
  {
    case NoModifier:
      mLayer->selectByIds( featureIds );
      break;
    case AddToSelection:
      mLayer->modifySelection( featureIds, QgsFeatureIds() );
      break;
    case RemoveFromSelection:
      mLayer->modifySelection(QgsFeatureIds(),  featureIds );
      break;
  }
}

void QgsElevationProfileToolSelectFeatures::plotMoveEvent( QgsPlotMouseEvent *event )
{
  event->ignore();

  // don't allow the band to be dragged outside of the plot area
  const QRectF plotArea = qgis::down_cast<QgsElevationProfileCanvas *>( mCanvas )->plotArea();
  QPointF movePoint;
  if ( !plotArea.contains( event->pos() ) )
  {
    movePoint = constrainPointToRect( event->pos(), plotArea );
  }
  else
  {
    movePoint = event->snappedPoint().toQPointF();
  }

  mRubberBand->update( movePoint, Qt::KeyboardModifiers() );
}

void QgsElevationProfileToolSelectFeatures::setLayer( QgsVectorLayer *layer )
{
  mLayer = layer;
  toggleAction();
}

void QgsElevationProfileToolSelectFeatures::toggleAction()
{
  QAction *selectFeaturesAction = action();
  if ( !selectFeaturesAction )
    return;

  selectFeaturesAction->setEnabled( mLayer != nullptr );
}
