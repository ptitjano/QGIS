/***************************************************************************
  qgs3daxes.cpp
  --------------------------------------
  Date                 : March 2022
  Copyright            : (C) 2022 by Jean Felder
  Email                : jean dot felder at oslandia dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DExtras/QConeMesh>
#include <Qt3DExtras/QCylinderMesh>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/Qt3DWindow>
#include <Qt3DExtras/QText2DEntity>
#include <Qt3DRender/QCamera>
#include <Qt3DRender/QCameraSelector>
#include <Qt3DRender/QClearBuffers>
#include <Qt3DRender/QLayer>
#include <Qt3DRender/QLayerFilter>
#include <Qt3DRender/QViewport>

#include "qgs3daxes.h"

Qgs3dAxes::Qgs3dAxes( Qt3DExtras::Qt3DWindow *mainWindow, Qt3DRender::QCamera *mainCamera, Qt3DCore::QEntity *root )
{
  qDebug() << "Coucou Jean";
  mMainCamera = mainCamera;
  mMainWindow = mainWindow;

  mEntity = new Qt3DCore::QEntity( root );

  mCamera = new Qt3DRender::QCamera();
  mCamera->setParent( mEntity );
  qDebug() << mMainCamera->projectionType();
  // mCamera->setProjectionType( mMainCamera->projectionType() );
  mCamera->setProjectionType( Qt3DRender::QCameraLens::ProjectionType::OrthographicProjection );
  mCamera->lens()->setOrthographicProjection(
    -5.0f, 5.0f,
    -5.0f, 5.0f,
    0.1f, 25.0f );
  mCamera->setUpVector( mMainCamera->upVector() );
  mCamera->setViewCenter( QVector3D( 0.0f, 0.0f, 0.0f ) );

  mMainCamera->connect( mMainCamera, &Qt3DRender::QCamera::viewVectorChanged, this, &Qgs3dAxes::update );

  mViewport = new Qt3DRender::QViewport( mainWindow->activeFrameGraph() );

  Qt3DRender::QLayer *layer = new Qt3DRender::QLayer( mEntity );
  layer->setRecursive( true );
  Qt3DRender::QLayerFilter *layerFilter = new Qt3DRender::QLayerFilter( mViewport );
  layerFilter->addLayer( layer );
  mEntity->addComponent( layer);

  Qt3DRender::QCameraSelector *cameraSelector = new Qt3DRender::QCameraSelector( layerFilter );
  cameraSelector->setCamera( mCamera );
  Qt3DRender::QClearBuffers *clearBuffers = new Qt3DRender::QClearBuffers( cameraSelector );
  clearBuffers->setClearColor( Qt::yellow );
  clearBuffers->setBuffers( Qt3DRender::QClearBuffers::DepthBuffer );

  createScene();
  updateViewport();
}

void Qgs3dAxes::createScene()
{
  createAxis ( Axis::X );
  createAxis ( Axis::Y );
  createAxis ( Axis::Z );
}

void Qgs3dAxes::createAxis( const Qgs3dAxes::Axis &axis )
{
  float cylinderRadius = 0.05 * mCylinderLength;
  float coneLength = 0.3 * mCylinderLength;
  float coneBottomRadius = 0.1 * mCylinderLength;

  QQuaternion rotation;
  QColor color;
  QString axisName;
  QVector3D textTranslation;

  switch ( axis ) {
  case Axis::X:
    rotation = QQuaternion::fromAxisAndAngle( QVector3D( 0.0f, 0.0f, 1.0f ), -90.0f );
    color = Qt::red;
    axisName = QString("X");
    textTranslation = QVector3D( mCylinderLength, -mCylinderLength / 4.0f, 0.0f );
    break;
  case Axis::Y:
    rotation = QQuaternion::fromAxisAndAngle( QVector3D( 0.0f, 0.0f, 0.0f ), 0.0f );
    color = Qt::green;
    axisName = QString("Y");
    textTranslation = QVector3D( coneBottomRadius, (mCylinderLength + coneLength) / 2.0, 0.0f );
    break;
  case Axis::Z:
    rotation = QQuaternion::fromAxisAndAngle( QVector3D( 1.0f, 0.0f, 0.0f ), 90.0f );
    color = Qt::blue;
    axisName = QString("Z");
    textTranslation = QVector3D( 0.0f, -mCylinderLength / 4.0f, mCylinderLength + coneLength / 2.0 );
    break;
  default:
    return;
  }

  // cylinder
  Qt3DCore::QEntity *cylinder = new Qt3DCore::QEntity( mEntity );
  Qt3DExtras::QCylinderMesh *cylinderMesh = new Qt3DExtras::QCylinderMesh;
  cylinderMesh->setRadius( cylinderRadius );
  cylinderMesh->setLength( mCylinderLength );
  cylinderMesh->setRings( 100 );
  cylinderMesh->setSlices( 20 );
  cylinder->addComponent( cylinderMesh );

  Qt3DExtras::QPhongMaterial *cylinderMaterial = new Qt3DExtras::QPhongMaterial( cylinder );
  cylinderMaterial->setAmbient( color );
  cylinderMaterial->setShininess( 0 );
  cylinder->addComponent( cylinderMaterial );

  Qt3DCore::QTransform *cylinderTransform = new Qt3DCore::QTransform;
  QMatrix4x4 transformMatrixCylinder;
  transformMatrixCylinder.rotate( rotation );
  transformMatrixCylinder.translate( QVector3D( 0.0f, mCylinderLength / 2.0f, 0.0f ) );
  cylinderTransform->setMatrix (transformMatrixCylinder );
  cylinder->addComponent( cylinderTransform );

  // cone
  Qt3DCore::QEntity *coneEntity = new Qt3DCore::QEntity( mEntity );
  Qt3DExtras::QConeMesh *coneMesh = new Qt3DExtras::QConeMesh;
  coneMesh->setLength( coneLength );
  coneMesh->setBottomRadius( coneBottomRadius );
  coneMesh->setTopRadius( 0.0f );
  coneMesh->setRings( 100 );
  coneMesh->setSlices( 20 );
  coneEntity->addComponent( coneMesh );

  Qt3DExtras::QPhongMaterial *coneMaterial = new Qt3DExtras::QPhongMaterial( coneEntity );
  coneMaterial->setAmbient( color );
  coneMaterial->setShininess( 0 );
  coneEntity->addComponent( coneMaterial );

  Qt3DCore::QTransform *coneTransform = new Qt3DCore::QTransform;
  QMatrix4x4 transformMatrixCone;
  transformMatrixCone.rotate( rotation );
  transformMatrixCone.translate( QVector3D( 0.0f, mCylinderLength, 0.0f ) );
  coneTransform->setMatrix (transformMatrixCone );
  coneEntity->addComponent( coneTransform );

  // axis name
  Qt3DExtras::QText2DEntity *text = new Qt3DExtras::QText2DEntity( mEntity );
  text->setFont( QFont( "monospace", 8 ) );
  text->setHeight( 20 );
  text->setWidth( 100 );
  text->setText( axisName );
  text->setColor( Qt::black );

  Qt3DCore::QTransform *textTransform = new Qt3DCore::QTransform;
  textTransform->setScale( 0.1f );
  textTransform->setTranslation( textTranslation );
  text->addComponent( textTransform );
}

void Qgs3dAxes::update()
{
  qDebug() << "Axis, update";
  updateCamera();
}

void Qgs3dAxes::updateViewport()
{
  float widthRatio = 120.0f / mMainWindow->width();
  float heightRatio = 120.0f / mMainWindow->height();

  qDebug() << "Axis, update viewport" << widthRatio << heightRatio;
  mViewport->setNormalizedRect( QRectF( 1.0 - widthRatio, 1.0 - heightRatio, widthRatio, heightRatio ) );
}

void Qgs3dAxes::updateCamera()
{
  // FIXME: these are magic numbers
  // There must be a reliable way to compute them
  qDebug() << "update camera";
  // qDebug() << "axis camara viewcenter";

  QVector3D mainCameraShift = ( mMainCamera->position() - mMainCamera->viewCenter() ).normalized();
  qDebug() << "update camera, main Camera shift" << mainCameraShift;
  mCamera->setPosition( mainCameraShift * 5.2 * mCylinderLength );
  // qDebug() << "Axis, new camera position" << mCamera->position();

  qDebug() << "update finshed, new axis camera position" << mCamera->position();
}
