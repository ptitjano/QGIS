/***************************************************************************
  qgs3daxes.h
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

#ifndef QGS3DAXES_H
#define QGS3DAXES_H

#include <QSurface>

namespace Qt3DCore {
  class QEntity;
}

namespace Qt3DRender {
  class QCamera;
  class QViewport;
}

namespace Qt3DExtras {
  class Qt3DWindow;
}

#define SIP_NO_FILE

class Qgs3dAxes : public QObject
{
    Q_OBJECT
  public:
    Qgs3dAxes( Qt3DExtras::Qt3DWindow *mainWindow, Qt3DRender::QCamera *mainCamera, Qt3DCore::QEntity *root );
    void updateViewport();

    enum Axis
      {
        X = 1,
        Y = 2,
        Z = 3
      };

  private:
    void createScene();
    void createAxis( const Axis &axis );
    void update();
    void updateCamera();

    Qt3DExtras::Qt3DWindow *mMainWindow = nullptr;
    Qt3DRender::QCamera *mMainCamera = nullptr;
    Qt3DCore::QEntity *mEntity = nullptr;
    Qt3DRender::QCamera *mCamera = nullptr;
    Qt3DRender::QViewport *mViewport = nullptr;
    float mCylinderLength = 4.0f;
};

#endif // QGS3DAXES_H
