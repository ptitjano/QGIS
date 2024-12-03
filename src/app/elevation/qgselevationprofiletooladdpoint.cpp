/***************************************************************************
                          qgselevationprofiletooladdpoint.cpp
                          ---------------
    begin                : September 2024
    copyright            : (C) 2024 by Simon Lopez
    email                : simon dot lopez at free dot fr
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgselevationprofiletooladdpoint.h"
#include "moc_qgselevationprofiletooladdpoint.cpp"
#include "qgsmessagebar.h"
#include "qgsplotmouseevent.h"
#include "qgsapplication.h"
#include "qgselevationprofilecanvas.h"
#include "qgisapp.h"
#include "qgsvectorlayer.h"
#include "qgsfeatureaction.h"
#include "qgssettingsregistrycore.h"

QgsElevationProfileToolAddPoint::QgsElevationProfileToolAddPoint( QgsElevationProfileCanvas *canvas )
  : QgsPlotTool( canvas, tr( "Add Point Feature" ) )
{
  setCursor( QgsApplication::getThemeCursor( QgsApplication::Cursor::CapturePoint ) );
}

QgsElevationProfileToolAddPoint::~QgsElevationProfileToolAddPoint() = default;

void QgsElevationProfileToolAddPoint::plotReleaseEvent( QgsPlotMouseEvent *event )
{
  event->ignore();
  if ( event->button() != Qt::LeftButton )
  {
    return;
  }

  if ( !mLayer )
  {
    return;
  }

  const Qgis::WkbType layerWKBType = mLayer->wkbType();

  const QgsPoint mapPoint = mCanvas->toMapCoordinates( event->snappedPoint() );
  const QgsGeometry geometry( std::make_unique<QgsPoint>( mapPoint ) );
  if ( geometry.isNull() || geometry.isEmpty() )
  {
    QgisApp::instance()->messageBar()->pushWarning( tr( "Add point" ), tr( "Could not add point: no profile curve" ) );
    QgsDebugError( QStringLiteral( "Could not add point with no geometry" ) );
    return;
  }

  QgsGeometry layerGeometry;
  double defaultZ = QgsSettingsRegistryCore::settingsDigitizingDefaultZValue->value();
  double defaultM = QgsSettingsRegistryCore::settingsDigitizingDefaultMValue->value();
  QVector<QgsGeometry> layerGeometries = geometry.coerceToType( layerWKBType, defaultZ, defaultM );
  if ( layerGeometries.count() > 0 )
  {
    layerGeometry = layerGeometries.at( 0 );
  }

  if ( layerGeometry.wkbType() != layerWKBType && layerGeometry.wkbType() != QgsWkbTypes::linearType( layerWKBType ) )
  {
    QgsDebugError( QStringLiteral( "The digitized geometry type (%1) does not correspond to the layer geometry type (%2)." ).arg( QgsWkbTypes::displayString( layerGeometry.wkbType() ), QgsWkbTypes::displayString( layerWKBType ) ) );
    return;
  }

  QgsCoordinateTransform transform( mCanvas->crs(), mLayer->crs3D(), QgsProject::instance()->transformContext() );
  transform.setBallparkTransformsAreAppropriate( true );
  try
  {
    layerGeometry.transform( transform, Qgis::TransformDirection::Forward, true );
  }
  catch ( QgsCsException &cse )
  {
    Q_UNUSED( cse )
    QgsDebugError( QStringLiteral( "Caught CRS exception %1" ).arg( cse.what() ) );
    return;
  }

  QgsFeature feature( mLayer->fields(), 0 );
  feature.setGeometry( layerGeometry );
  feature.setValid( true );

  QgsFeatureAction *action = new QgsFeatureAction( tr( "add feature" ), feature, mLayer, QUuid(), -1, this );
  const QgsFeatureAction::AddFeatureResult res = action->addFeature( QgsAttributeMap(), true, nullptr, false, nullptr );
  if ( res != QgsFeatureAction::AddFeatureResult::Success )
  {
    QgsDebugError( QStringLiteral( "Unable to create new feature" ) );
  }
  delete action;
}

void QgsElevationProfileToolAddPoint::setLayer( QgsVectorLayer *layer )
{
  if ( layer == mLayer )
  {
    return;
  }

  if ( mLayer )
  {
    disconnect( mLayer, &QgsVectorLayer::editingStarted, this, &QgsElevationProfileToolAddPoint::toggleAction );
    disconnect( mLayer, &QgsVectorLayer::editingStopped, this, &QgsElevationProfileToolAddPoint::toggleAction );
    // disconnect( mLayer, &QgsVectorLayer::destroyed, this, &QgsGeometryValidationDock::onLayerDestroyed );
  }

  mLayer = layer;

  if ( mLayer )
  {
    connect( mLayer, &QgsVectorLayer::editingStarted, this, &QgsElevationProfileToolAddPoint::toggleAction );
    connect( mLayer, &QgsVectorLayer::editingStopped, this, &QgsElevationProfileToolAddPoint::toggleAction );
  }

  toggleAction();
}

void QgsElevationProfileToolAddPoint::toggleAction()
{
  QAction *addPointAction = action();
  if ( !addPointAction )
    return;

  if ( mLayer )
  {
    const bool isPoint = mLayer->geometryType() == Qgis::GeometryType::Point;
    const bool canAddFeatures = mLayer->dataProvider()->capabilities() & Qgis::VectorProviderCapability::AddFeatures;
    addPointAction->setEnabled( isPoint && canAddFeatures && mLayer->isEditable() );
  }
  else
  {
    addPointAction->setEnabled( false );
  }
}

void QgsElevationProfileToolAddPoint::onLayerDestroyed( QObject *layer )
{
  if ( layer == mLayer )
  {
    mLayer = nullptr;
  }
}
