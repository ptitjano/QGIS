/***************************************************************************
  qgschunkedentity_p.cpp
  --------------------------------------
  Date                 : July 2017
  Copyright            : (C) 2017 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgschunkedentity_p.h"

#include <QElapsedTimer>
#include <QVector4D>

#include "qgs3dutils.h"
#include "qgschunkboundsentity_p.h"
#include "qgschunklist_p.h"
#include "qgschunkloader_p.h"
#include "qgschunknode_p.h"

#include "qgseventtracing.h"

#include <queue>

/// BDE TO REMOVE:
#define QGISDEBUG 1
///@cond PRIVATE

static QString _logHeader( const QString &layerName )
{
  if ( layerName.isEmpty() )
    return QStringLiteral( "{layer:<not_set>} " );
  return QStringLiteral( "{layer:%1} " ).arg( layerName );
}

static float screenSpaceError( QgsChunkNode *node, const QgsChunkedEntity::SceneContext &sceneContext )
{
  if ( node->error() <= 0 ) //it happens for meshes
    return 0;

  float dist = node->bbox().distanceFromPoint( sceneContext.cameraPos );

  // TODO: what to do when distance == 0 ?

  float sse = Qgs3DUtils::screenSpaceError( node->error(), dist, sceneContext.screenSizePx, sceneContext.cameraFov );
  return sse;
}


static bool hasAnyActiveChildren( QgsChunkNode *node, QList<QgsChunkNode *> &activeNodes )
{
  for ( int i = 0; i < node->childCount(); ++i )
  {
    QgsChunkNode *child = node->children()[i];
    if ( child->entity() && activeNodes.contains( child ) )
      return true;
    if ( hasAnyActiveChildren( child, activeNodes ) )
      return true;
  }
  return false;
}


QgsChunkedEntity::QgsChunkedEntity( float tau, QgsChunkLoaderFactory *loaderFactory, bool ownsFactory, int primitiveBudget, Qt3DCore::QNode *parent )
  : Qgs3DMapSceneEntity( parent )
  , mTau( tau )
  , mChunkLoaderFactory( loaderFactory )
  , mOwnsFactory( ownsFactory )
  , mPrimitivesBudget( primitiveBudget )
{
  mRootNode = loaderFactory->createRootNode();
  mChunkLoaderQueue = new QgsChunkList;
  mReplacementQueue = new QgsChunkList;

  // in case the chunk loader factory supports fetching of hierarchy in background (to avoid GUI freezes)
  connect( loaderFactory, &QgsChunkLoaderFactory::childrenPrepared, this, [this]
  {
    setNeedsUpdate( true );
    emit pendingJobsCountChanged();
  } );
}


QgsChunkedEntity::~QgsChunkedEntity()
{
  // derived classes have to make sure that any pending active job has finished / been canceled
  // before getting to this destructor - here it would be too late to cancel them
  // (e.g. objects required for loading/updating have been deleted already)
  Q_ASSERT( mActiveJobs.isEmpty() );

  // clean up any pending load requests
  while ( !mChunkLoaderQueue->isEmpty() )
  {
    QgsChunkListEntry *entry = mChunkLoaderQueue->takeFirst();
    QgsChunkNode *node = entry->chunk;

    if ( node->state() == QgsChunkNode::QueuedForLoad )
      node->cancelQueuedForLoad();
    else if ( node->state() == QgsChunkNode::QueuedForUpdate )
      node->cancelQueuedForUpdate();
    else
      Q_ASSERT( false );  // impossible!

    delete entry; // created here, deleted here
  }

  delete mChunkLoaderQueue;
  mChunkLoaderQueue = nullptr;

  while ( !mReplacementQueue->isEmpty() )
  {
    QgsChunkListEntry *entry = mReplacementQueue->takeFirst();

    // remove loaded data from node
    entry->chunk->unloadChunk(); // also deletes the entity!
  }

  delete mReplacementQueue;
  mReplacementQueue = nullptr;
  delete mRootNode;
  mRootNode = nullptr;

  if ( mOwnsFactory )
  {
    delete mChunkLoaderFactory;
  }
  mChunkLoaderFactory = nullptr;
}

void QgsChunkedEntity::setHasReachedGpuMemoryLimit( bool reached )
{
  if ( mHasReachedGpuMemoryLimit != reached )
  {
    mHasReachedGpuMemoryLimit = reached;
    if ( reached )
    {
      if ( mActiveJobs.isEmpty() && mChunkLoaderQueue->isEmpty() && mReplacementQueue->isEmpty() )
        return;

      QgsDebugMsgLevel( _logHeader( mLayerName )
                        + QStringLiteral( "has reached gpu memory limit. Cleaning up: active %1 | culled %2 | loading %3 loaded %4" )
                        .arg( mActiveNodes.count() )
                        .arg( mFrustumCulled )
                        .arg( mChunkLoaderQueue->count() )
                        .arg( mReplacementQueue->count() ), QGS_LOG_LVL_DEBUG );

      while ( !mActiveJobs.isEmpty() )
      {
        QgsChunkQueueJob *job = mActiveJobs.takeFirst();
        job->cancel();
        job->deleteLater();
      }

      while ( !mChunkLoaderQueue->isEmpty() )
      {
        mChunkLoaderQueue->takeFirst();
      }

      // We do not prune mReplacementQueue as the entries are created in the QgsChunkNode and they will lose prev/next references
      // If we delete the QgsChunkNode::mReplacementQueueEntry in the QgsChunkNode::freeze function, we will need to recreate it
      // later when the layer is un-frozen. This mechanism will be too ricky.
      mRootNode->freezeAllChildren();

      emit pendingJobsCountChanged();
    }
  }
}

void QgsChunkedEntity::handleSceneUpdate( const SceneContext &sceneContext, double availableGpuMemory )
{
  mLastKnownAvailableGpuMemory = availableGpuMemory;

  if ( !mIsValid || mHasReachedGpuMemoryLimit )
    return;

  // Let's start the update by removing from loader queue chunks that
  // would get frustum culled if loaded (outside of the current view
  // of the camera). Removing them keeps the loading queue shorter,
  // and we avoid loading chunks that we only wanted for a short period
  // of time when camera was moving.
  pruneLoaderQueue( sceneContext );

  QElapsedTimer t;
  t.start();

  int oldJobsCount = pendingJobsCount();

  QSet<QgsChunkNode *> activeBefore = qgis::listToSet( mActiveNodes );
  mActiveNodes.clear();
  mFrustumCulled = 0;
  mCurrentTime = QTime::currentTime();

  update( mRootNode, sceneContext );

#ifdef QGISDEBUG
  int enabled = 0, disabled = 0, unloaded = 0;
#endif

  for ( QgsChunkNode *node : std::as_const( mActiveNodes ) )
  {
    if ( activeBefore.contains( node ) )
    {
      activeBefore.remove( node );
    }
    else
    {
      if ( !node->entity() )
      {
        QgsDebugError( _logHeader( mLayerName )
                       + QString( "Active node %1 has null entity - this should never happen!" )
                       .arg( node->tileId().text() ) );
        continue;
      }
      node->entity()->setEnabled( true );
#ifdef QGISDEBUG
      ++enabled;
#endif
    }
  }

  // disable those that were active but will not be anymore
  for ( QgsChunkNode *node : activeBefore )
  {
    if ( !node->entity() )
    {
      QgsDebugError( _logHeader( mLayerName )
                     + QString( "Active node %1 has null entity - this should never happen!" )
                     .arg( node->tileId().text() ) );
      continue;
    }
    node->entity()->setEnabled( false );
#ifdef QGISDEBUG
    ++disabled;
#endif
  }

  // if this entity's loaded nodes are using more GPU memory than allowed,
  // let's try to unload those that are not needed right now
#ifdef QGISDEBUG
  unloaded = unloadNodes();
#else
  unloadNodes();
#endif

  if ( mBboxesEntity )
  {
    QList<QgsAABB> bboxes;
    for ( QgsChunkNode *n : std::as_const( mActiveNodes ) )
      bboxes << n->bbox();
    mBboxesEntity->setBoxes( bboxes );
  }

  // start a job from queue if there is anything waiting
  startJobs();

  mNeedsUpdate = false;  // just updated

  if ( pendingJobsCount() != oldJobsCount )
    emit pendingJobsCountChanged();

#ifdef QGISDEBUG
  QgsDebugMsgLevel( _logHeader( mLayerName )
                    + QStringLiteral( "update: enabled %1 disabled %2 | unloaded %3" )
                    .arg( enabled )
                    .arg( disabled )
                    .arg( unloaded ), QGS_LOG_LVL_TRACE );
#endif
  QgsDebugMsgLevel( _logHeader( mLayerName )
                    + QStringLiteral( "update: active %1 | culled %2 | loading %3 loaded %4 | elapsed %5ms" )
                    .arg( mActiveNodes.count() )
                    .arg( mFrustumCulled )
                    .arg( mChunkLoaderQueue->count() )
                    .arg( mReplacementQueue->count() )
                    .arg( t.elapsed() ), QGS_LOG_LVL_DEBUG );
}


int QgsChunkedEntity::unloadNodes()
{
  double currentlyUsedGpuMemory = Qgs3DUtils::calculateEntityGpuMemorySize( this );
  if ( mUsedGpuMemory < currentlyUsedGpuMemory )
    QgsDebugMsgLevel( _logHeader( mLayerName )
                      + QStringLiteral( "GPU memory usage increased! (now_using: %1MB, was: %2MB)" )
                      .arg( currentlyUsedGpuMemory )
                      .arg( mUsedGpuMemory ), QGS_LOG_LVL_DEBUG );

  double usableGpuMemory = mLastKnownAvailableGpuMemory + mUsedGpuMemory;
  if ( currentlyUsedGpuMemory <= usableGpuMemory )
  {
    QgsDebugMsgLevel( _logHeader( mLayerName )
                      + QStringLiteral( "Nothing to unload! (now_using: %1MB, limit: %2MB)" )
                      .arg( currentlyUsedGpuMemory )
                      .arg( usableGpuMemory ), QGS_LOG_LVL_TRACE );
    mUsedGpuMemory = currentlyUsedGpuMemory;
    setHasReachedGpuMemoryLimit( false );
    return 0;
  }

  QgsDebugMsgLevel( _logHeader( mLayerName )
                    + QStringLiteral( "Going to unload nodes to free GPU memory (was_used: %1MB, now_using: %2MB, limit: %3MB)" )
                    .arg( mUsedGpuMemory )
                    .arg( currentlyUsedGpuMemory )
                    .arg( usableGpuMemory ), QGS_LOG_LVL_DEBUG );

  int unloaded = 0;

  // unload nodes starting from the back of the queue with currently loaded
  // nodes - i.e. those that have been least recently used
  QgsChunkListEntry *entry = mReplacementQueue->last();
  while ( entry && currentlyUsedGpuMemory > usableGpuMemory )
  {
    // not all nodes are safe to unload: we do not want to unload nodes
    // that are currently active, or have their descendants active or their
    // siblings or their descendants are active (because in the next scene
    // update, these would be very likely loaded again, making the unload worthless)
    if ( entry->chunk->parent() && !hasAnyActiveChildren( entry->chunk->parent(), mActiveNodes ) )
    {
      QgsChunkListEntry *entryPrev = entry->prev;
      mReplacementQueue->takeEntry( entry );
      currentlyUsedGpuMemory -= Qgs3DUtils::calculateEntityGpuMemorySize( entry->chunk->entity() );
      mActiveNodes.removeOne( entry->chunk );
      if ( entry->chunk->state() == QgsChunkNode::Loaded ) // will unload a real loaded node. It may have been frozen during the interval
        entry->chunk->unloadChunk(); // also deletes the entity!
      ++unloaded;
      entry = entryPrev;
    }
    else
    {
      entry = entry->prev;
    }
  }

  if ( currentlyUsedGpuMemory > usableGpuMemory )
  {
    setHasReachedGpuMemoryLimit( true );
    QgsDebugMsgLevel( _logHeader( mLayerName )
                      + QStringLiteral( "Unable to unload enough nodes to free GPU memory (was_used: %1 MB, now_using: %2 MB, limit: %3 MB)" )
                      .arg( mUsedGpuMemory )
                      .arg( currentlyUsedGpuMemory )
                      .arg( usableGpuMemory ), QGS_LOG_LVL_DEBUG );
  }

  mLastKnownAvailableGpuMemory += ( currentlyUsedGpuMemory - mUsedGpuMemory );
  mUsedGpuMemory = currentlyUsedGpuMemory;

  return unloaded;
}


QgsRange<float> QgsChunkedEntity::getNearFarPlaneRange( const QMatrix4x4 &viewMatrix ) const
{
  QList<QgsChunkNode *> activeEntityNodes = activeNodes();

  // it could be that there are no active nodes - they could be all culled or because root node
  // is not yet loaded - we still need at least something to understand bounds of our scene
  // so lets use the root node
  if ( activeEntityNodes.empty() )
    activeEntityNodes << rootNode();

  float fnear = 1e9;
  float ffar = 0;

  for ( QgsChunkNode *node : std::as_const( activeEntityNodes ) )
  {
    // project each corner of bbox to camera coordinates
    // and determine closest and farthest point.
    QgsAABB bbox = node->bbox();
    float bboxfnear;
    float bboxffar;
    Qgs3DUtils::computeBoundingBoxNearFarPlanes( bbox, viewMatrix, bboxfnear, bboxffar );
    fnear = std::min( fnear, bboxfnear );
    ffar = std::max( ffar, bboxffar );
  }
  return QgsRange<float>( fnear, ffar );
}

void QgsChunkedEntity::setShowBoundingBoxes( bool enabled )
{
  if ( ( enabled && mBboxesEntity ) || ( !enabled && !mBboxesEntity ) )
    return;

  if ( enabled )
  {
    mBboxesEntity = new QgsChunkBoundsEntity( this );
  }
  else
  {
    mBboxesEntity->deleteLater();
    mBboxesEntity = nullptr;
  }
}

void QgsChunkedEntity::updateNodes( const QList<QgsChunkNode *> &nodes, QgsChunkQueueJobFactory *updateJobFactory )
{
  for ( QgsChunkNode *node : nodes )
  {
    if ( mHasReachedGpuMemoryLimit )
      return;

    if ( node->state() == QgsChunkNode::QueuedForUpdate )
    {
      QgsChunkListEntry *entry = node->loaderQueueEntry();
      mChunkLoaderQueue->takeEntry( entry );
      node->cancelQueuedForUpdate();
      delete entry;
    }
    else if ( node->state() == QgsChunkNode::Updating )
    {
      cancelActiveJob( node->updater() );
    }

    Q_ASSERT( node->state() == QgsChunkNode::Loaded );

    QgsChunkListEntry *entry = new QgsChunkListEntry( node );
    node->setQueuedForUpdate( entry, updateJobFactory );
    mChunkLoaderQueue->insertLast( entry );
  }

  // trigger update
  startJobs();
}

void QgsChunkedEntity::pruneLoaderQueue( const SceneContext &sceneContext )
{
  QList<QgsChunkNode *> toRemoveFromLoaderQueue;

  // Step 1: collect all entries from chunk loader queue that would get frustum culled
  // (i.e. they are outside of the current view of the camera) and therefore loading
  // such chunks would be probably waste of time.
  QgsChunkListEntry *e = mChunkLoaderQueue->first();
  while ( e )
  {
    if ( e->chunk->state() == QgsChunkNode::QueuedForLoad || e->chunk->state() == QgsChunkNode::QueuedForUpdate )
      if ( Qgs3DUtils::isCullable( e->chunk->bbox(), sceneContext.viewProjectionMatrix ) )
      {
        toRemoveFromLoaderQueue.append( e->chunk );
      }
    e = e->next;
  }

  // Step 2: remove collected chunks from the loading queue
  for ( QgsChunkNode *node : toRemoveFromLoaderQueue )
  {
    QgsChunkListEntry *entry = node->loaderQueueEntry();
    mChunkLoaderQueue->takeEntry( entry );
    if ( node->state() == QgsChunkNode::QueuedForLoad )
    {
      node->cancelQueuedForLoad();
    }
    else  // queued for update
    {
      node->cancelQueuedForUpdate();
      mReplacementQueue->takeEntry( node->replacementQueueEntry() );
      node->unloadChunk(); // also deletes the entity!
    }
    delete entry;
  }

  if ( !toRemoveFromLoaderQueue.isEmpty() )
  {
    QgsDebugMsgLevel( _logHeader( mLayerName )
                      + QStringLiteral( "Pruned %1 chunks in loading queue" ).arg( toRemoveFromLoaderQueue.count() ), QGS_LOG_LVL_DEBUG );
  }
}


int QgsChunkedEntity::pendingJobsCount() const
{
  return mChunkLoaderQueue->count() + mActiveJobs.count();
}

struct ResidencyRequest
{
  QgsChunkNode *node = nullptr;
  float dist = 0.0;
  int level = -1;
  ResidencyRequest() = default;
  ResidencyRequest(
    QgsChunkNode *n,
    float d,
    int l )
    : node( n )
    , dist( d )
    , level( l )
  {}
};

struct
{
  bool operator()( const ResidencyRequest &request, const ResidencyRequest &otherRequest ) const
  {
    if ( request.level == otherRequest.level )
      return request.dist > otherRequest.dist;
    return request.level > otherRequest.level;
  }
} ResidencyRequestSorter;

void QgsChunkedEntity::update( QgsChunkNode *root, const SceneContext &sceneContext )
{
  if ( mHasReachedGpuMemoryLimit )
    return;

  QSet<QgsChunkNode *> nodes;
  QVector<ResidencyRequest> residencyRequests;

  using slotItem = std::pair<QgsChunkNode *, float>;
  auto cmp_funct = []( const slotItem & p1, const slotItem & p2 )
  {
    return p1.second <= p2.second;
  };
  int renderedCount = 0;
  std::priority_queue<slotItem, std::vector<slotItem>, decltype( cmp_funct )> pq( cmp_funct );
  pq.push( std::make_pair( root, screenSpaceError( root, sceneContext ) ) );
  while ( !pq.empty() && renderedCount <= mPrimitivesBudget )
  {
    slotItem s = pq.top();
    pq.pop();
    QgsChunkNode *node = s.first;

    if ( Qgs3DUtils::isCullable( node->bbox(), sceneContext.viewProjectionMatrix ) )
    {
      ++mFrustumCulled;
      continue;
    }

    // ensure we have child nodes (at least skeletons) available, if any
    if ( !node->hasChildrenPopulated() )
    {
      // Some chunked entities (e.g. tiled scene) may not know the full node hierarchy in advance
      // and need to fetch it from a remote server. Having a blocking network request
      // in createChildren() is not wanted because this code runs on the main thread and thus
      // would cause GUI freezes. Here is a mechanism to first check whether there are any
      // network requests needed (with canCreateChildren()), and if that's the case,
      // prepareChildren() will start those requests in the background and immediately returns.
      // The factory will emit a signal when hierarchy fetching is done to force another update
      // of this entity to create children of this node.
      if ( mChunkLoaderFactory->canCreateChildren( node ) )
      {
        node->populateChildren( mChunkLoaderFactory->createChildren( node ) );
      }
      else
      {
        mChunkLoaderFactory->prepareChildren( node );
      }
    }

    // make sure all nodes leading to children are always loaded
    // so that zooming out does not create issues
    double dist = node->bbox().center().distanceToPoint( sceneContext.cameraPos );
    residencyRequests.push_back( ResidencyRequest( node, dist, node->level() ) );

    if ( !node->entity() )
    {
      // this happens initially when root node is not ready yet
      continue;
    }
    bool becomesActive = false;

    // QgsDebugMsgLevel( QStringLiteral( "%1|%2|%3  %4  %5" ).arg( node->tileId().x ).arg( node->tileId().y ).arg( node->tileId().z ).arg( mTau ).arg( screenSpaceError( node, sceneContext ) ), QGS_LOG_LVL_DEBUG );
    if ( node->childCount() == 0 )
    {
      // there's no children available for this node, so regardless of whether it has an acceptable error
      // or not, it's the best we'll ever get...
      becomesActive = true;
    }
    else if ( mTau > 0 && screenSpaceError( node, sceneContext ) <= mTau )
    {
      // acceptable error for the current chunk - let's render it
      becomesActive = true;
    }
    else
    {
      // This chunk does not have acceptable error (it does not provide enough detail)
      // so we'll try to use its children. The exact logic depends on whether the entity
      // has additive strategy. With additive strategy, child nodes should be rendered
      // in addition to the parent nodes (rather than child nodes replacing parent entirely)

      if ( node->refinementProcess() == Qgis::TileRefinementProcess::Additive )
      {
        // Logic of the additive strategy:
        // - children that are not loaded will get requested to be loaded
        // - children that are already loaded get recursively visited
        becomesActive = true;

        QgsChunkNode *const *children = node->children();
        for ( int i = 0; i < node->childCount(); ++i )
        {
          if ( children[i]->entity() || !children[i]->hasData() )
          {
            // chunk is resident - let's visit it recursively
            pq.push( std::make_pair( children[i], screenSpaceError( children[i], sceneContext ) ) );
          }
          else
          {
            // chunk is not yet resident - let's try to load it
            if ( Qgs3DUtils::isCullable( children[i]->bbox(), sceneContext.viewProjectionMatrix ) )
              continue;

            double dist = children[i]->bbox().center().distanceToPoint( sceneContext.cameraPos );
            residencyRequests.push_back( ResidencyRequest( children[i], dist, children[i]->level() ) );
          }
        }
      }
      else
      {
        // Logic of the replace strategy:
        // - if we have all children loaded, we use them instead of the parent node
        // - if we do not have all children loaded, we request to load them and keep using the parent for the time being
        if ( node->allChildChunksResident( mCurrentTime ) )
        {
          QgsChunkNode *const *children = node->children();
          for ( int i = 0; i < node->childCount(); ++i )
            pq.push( std::make_pair( children[i], screenSpaceError( children[i], sceneContext ) ) );
        }
        else
        {
          becomesActive = true;

          QgsChunkNode *const *children = node->children();
          for ( int i = 0; i < node->childCount(); ++i )
          {
            double dist = children[i]->bbox().center().distanceToPoint( sceneContext.cameraPos );
            residencyRequests.push_back( ResidencyRequest( children[i], dist, children[i]->level() ) );
          }
        }
      }
    }

    if ( becomesActive )
    {
      if ( !node->entity() )
      {
        QgsDebugError( _logHeader( mLayerName )
                       + QString( "Active node %1 has null entity - this should never happen!" )
                       .arg( node->tileId().text() ) );
      }
      else
      {
        mActiveNodes << node;
        // if we are not using additive strategy we need to make sure the parent primitives are not counted
        if ( node->refinementProcess() != Qgis::TileRefinementProcess::Additive && node->parent() && nodes.contains( node->parent() ) )
        {
          nodes.remove( node->parent() );
          renderedCount -= mChunkLoaderFactory->primitivesCount( node->parent() );
        }
        renderedCount += mChunkLoaderFactory->primitivesCount( node );
        nodes.insert( node );
      }
    }
  }

  // sort nodes by their level and their distance from the camera
  std::sort( residencyRequests.begin(), residencyRequests.end(), ResidencyRequestSorter );
  for ( const auto &request : residencyRequests )
    requestResidency( request.node );
}

void QgsChunkedEntity::requestResidency( QgsChunkNode *node )
{
  if ( node->state() == QgsChunkNode::Loaded || node->state() == QgsChunkNode::QueuedForUpdate || node->state() == QgsChunkNode::Updating )
  {
    Q_ASSERT( node->replacementQueueEntry() );
    Q_ASSERT( node->entity() );
    mReplacementQueue->takeEntry( node->replacementQueueEntry() );
    mReplacementQueue->insertFirst( node->replacementQueueEntry() );
  }
  else if ( node->state() == QgsChunkNode::QueuedForLoad )
  {
    // move to the front of loading queue
    Q_ASSERT( node->loaderQueueEntry() );
    Q_ASSERT( !node->loader() );
    if ( node->loaderQueueEntry()->prev || node->loaderQueueEntry()->next )
    {
      mChunkLoaderQueue->takeEntry( node->loaderQueueEntry() );
      mChunkLoaderQueue->insertFirst( node->loaderQueueEntry() );
    }
  }
  else if ( node->state() == QgsChunkNode::Loading )
  {
    // the entry is being currently processed - nothing to do really
  }
  else if ( node->state() == QgsChunkNode::Skeleton )
  {
    if ( !node->hasData() )
      return;   // no need to load (we already tried but got nothing back)

    // add to the loading queue
    QgsChunkListEntry *entry = new QgsChunkListEntry( node );
    node->setQueuedForLoad( entry );
    mChunkLoaderQueue->insertFirst( entry );
  }
  else
    Q_ASSERT( false && "impossible!" );
}


void QgsChunkedEntity::onActiveJobFinished()
{
  int oldJobsCount = pendingJobsCount();

  QgsChunkQueueJob *job = qobject_cast<QgsChunkQueueJob *>( sender() );
  Q_ASSERT( job );

  if ( mHasReachedGpuMemoryLimit )
  {
    // cleanup the job that has just finished
    mActiveJobs.removeOne( job );
    job->deleteLater();
    emit pendingJobsCountChanged();

    return;
  }

  Q_ASSERT( mActiveJobs.contains( job ) );

  // this <==> root entity --> node <==> sub node/chunk loaded by this
  QgsChunkNode *node = job->chunk();

  if ( QgsChunkLoader *loader = qobject_cast<QgsChunkLoader *>( job ) )
  {
    Q_ASSERT( node->state() == QgsChunkNode::Loading );
    Q_ASSERT( node->loader() == loader );

    QgsEventTracing::addEvent( QgsEventTracing::AsyncEnd, QStringLiteral( "3D" ), QStringLiteral( "Load " ) + node->tileId().text(), node->tileId().text() );
    QgsEventTracing::addEvent( QgsEventTracing::AsyncEnd, QStringLiteral( "3D" ), QStringLiteral( "Load" ), node->tileId().text() );

    QgsEventTracing::ScopedEvent e( "3D", QString( "create" ) );
    // mark as loaded + create entity
    Qt3DCore::QEntity *entity = node->loader()->createEntity( this );
    // entity <==> sub node entity (attached to this, ie. root entity)

    if ( entity )
    {
      double rootEntityGpuMemory = Qgs3DUtils::calculateEntityGpuMemorySize( this );
      double prevUsedGpuMemory = mUsedGpuMemory;
      double entityGpuMemory = rootEntityGpuMemory - prevUsedGpuMemory;
#ifdef QGISDEBUG
      // search layer name for log purpose
      Qgs3DMapSceneEntity *ent = dynamic_cast<Qgs3DMapSceneEntity *>( this );
      while ( ent->layerName().isEmpty() || ent->layerName() == "unknown" )
      {
        if ( ent->parent() && dynamic_cast<Qgs3DMapSceneEntity *>( ent->parent() ) )
          ent = dynamic_cast<Qgs3DMapSceneEntity *>( ent->parent() );
        else
          break;
      }
      QString ln = ent->layerName();
      if ( ln.isEmpty() || ln == "unknown" )
        ln = "unknown_deep";

      QgsDebugMsgLevel( _logHeader( ln )
                        + QStringLiteral( "Checking entity %1. new node_size: %2, old node_size: %3, cur entity_size: %4" )
                        .arg( node->tileId().text() )
                        .arg( rootEntityGpuMemory )
                        .arg( mUsedGpuMemory )
                        .arg( entityGpuMemory )
                        , QGS_LOG_LVL_DEBUG );
#endif

      // check memory consumption, ie. if we are near or will reach available memory
      if ( entityGpuMemory * mActiveJobs.size() / 2.0 > mLastKnownAvailableGpuMemory )
      {
        // we need to cancel this pre-loaded entity and set mHasReachedGpuMemoryLimit to true
        setHasReachedGpuMemoryLimit( true );
        if ( node->state() == QgsChunkNode::Loading ) // can be frozen from elsewhere
          node->cancelLoading();
#ifdef QGISDEBUG
        else
          QgsDebugMsgLevel( _logHeader( ln )
                            + QStringLiteral( "Entity %1 is no more in loading state (now %2)" )
                            .arg( node->tileId().text() )
                            .arg( node->state() ), QGS_LOG_LVL_DEBUG );
#endif
        entity->deleteLater(); // delete the failed sub entity now

        // compute what we can gain:
        double rootEntityGpuMemoryAfter = Qgs3DUtils::calculateEntityGpuMemorySize( this );
        mLastKnownAvailableGpuMemory -= ( rootEntityGpuMemoryAfter - rootEntityGpuMemory );
        mUsedGpuMemory = rootEntityGpuMemoryAfter;
#ifdef QGISDEBUG
        QgsDebugMsgLevel( _logHeader( ln )
                          + QStringLiteral( "Cancel loading %1. node_size: %2, availGpuMem: %3" )
                          .arg( node->tileId().text() )
                          .arg( mUsedGpuMemory )
                          .arg( mLastKnownAvailableGpuMemory ), QGS_LOG_LVL_DEBUG );
#endif
      }
      else
      {
        // we can keep this pre-loaded entity and add it to mActiveNodes
        mLastKnownAvailableGpuMemory -= ( rootEntityGpuMemory - prevUsedGpuMemory );
        mUsedGpuMemory = rootEntityGpuMemory;
#ifdef QGISDEBUG
        QgsDebugMsgLevel( _logHeader( ln )
                          + QStringLiteral( "New entity loaded %1. node_size: %2, availGpuMem: %3" )
                          .arg( node->tileId().text() )
                          .arg( mUsedGpuMemory )
                          .arg( mLastKnownAvailableGpuMemory ), QGS_LOG_LVL_DEBUG );
#endif

        // The returned QEntity is initially enabled, so let's add it to active nodes too.
        // Soon afterwards updateScene() will be called, which would remove it from the scene
        // if the node should not be shown anymore. Ideally entities should be initially disabled,
        // but there seems to be a bug in Qt3D - if entity is disabled initially, showing it
        // by setting setEnabled(true) is not reliable (entity eventually gets shown, but only after
        // some more changes in the scene) - see https://github.com/qgis/QGIS/issues/48334
        mActiveNodes << node;

        // load into node (should be in main thread again)
        node->setLoaded( entity );

        mReplacementQueue->insertFirst( node->replacementQueueEntry() );

        emit newEntityCreated( entity, entityGpuMemory );
      }
    }
    else // entity is NULL
    {
      node->setHasData( false );
      node->cancelLoading();
    }

    loader->deleteLater();

    // now we need an update!
    mNeedsUpdate = true;
  }
  else // job is NOT a QgsChunkLoader
  {
    if ( ! mHasReachedGpuMemoryLimit ) // if mHasReachedGpuMemoryLimit some job cannot be finished yet, node will have bad state
    {
      if ( node->state() != QgsChunkNode::Updating )
      {
        QgsDebugMsgLevel( QStringLiteral( "Node %1 should be in state Updating but is state: '%2'" )
                          .arg( node->tileId().text() )
                          .arg( node->state() ), QGS_LOG_LVL_WARNING );
      }
      else
      {
        QgsEventTracing::addEvent( QgsEventTracing::AsyncEnd, QStringLiteral( "3D" ), QStringLiteral( "Update" ), node->tileId().text() );
        node->setUpdated();
      }
    }
  }

  // cleanup the job that has just finished
  mActiveJobs.removeOne( job );
  job->deleteLater();

  // start another job - if any
  startJobs();

  if ( pendingJobsCount() != oldJobsCount )
    emit pendingJobsCountChanged();
}

void QgsChunkedEntity::startJobs()
{
  while ( !mHasReachedGpuMemoryLimit && mActiveJobs.count() < 4 && !mChunkLoaderQueue->isEmpty() )
  {
    QgsChunkListEntry *entry = mChunkLoaderQueue->takeFirst();
    Q_ASSERT( entry );
    QgsChunkNode *node = entry->chunk;
    delete entry;

    QgsChunkQueueJob *job = startJob( node );
    mActiveJobs.append( job );
  }
}

QgsChunkQueueJob *QgsChunkedEntity::startJob( QgsChunkNode *node )
{
  if ( mHasReachedGpuMemoryLimit )
  {
    node->freeze();
    return nullptr;
  }

  if ( node->state() == QgsChunkNode::QueuedForLoad )
  {
    QgsEventTracing::addEvent( QgsEventTracing::AsyncBegin, QStringLiteral( "3D" ), QStringLiteral( "Load" ), node->tileId().text() );
    QgsEventTracing::addEvent( QgsEventTracing::AsyncBegin, QStringLiteral( "3D" ), QStringLiteral( "Load " ) + node->tileId().text(), node->tileId().text() );

    QgsChunkLoader *loader = mChunkLoaderFactory->createChunkLoader( node );
    connect( loader, &QgsChunkQueueJob::finished, this, &QgsChunkedEntity::onActiveJobFinished );
    node->setLoading( loader );
    return loader;
  }
  else if ( node->state() == QgsChunkNode::QueuedForUpdate )
  {
    QgsEventTracing::addEvent( QgsEventTracing::AsyncBegin, QStringLiteral( "3D" ), QStringLiteral( "Update" ), node->tileId().text() );

    node->setUpdating();
    connect( node->updater(), &QgsChunkQueueJob::finished, this, &QgsChunkedEntity::onActiveJobFinished );
    return node->updater();
  }
  else
  {
    Q_ASSERT( false );  // not possible
    return nullptr;
  }
}

void QgsChunkedEntity::cancelActiveJob( QgsChunkQueueJob *job )
{
  if ( !job || !mActiveJobs.contains( job ) )
  {
    QgsDebugError( _logHeader( mLayerName )
                   + "Cannot cancel null job!" );
    return;
  }

  QgsChunkNode *node = job->chunk();
  disconnect( job, &QgsChunkQueueJob::finished, this, &QgsChunkedEntity::onActiveJobFinished );

  if ( dynamic_cast<QgsChunkLoader *>( job ) )
  {
    // return node back to skeleton
    node->cancelLoading();

    QgsEventTracing::addEvent( QgsEventTracing::AsyncEnd, QStringLiteral( "3D" ), QStringLiteral( "Load " ) + node->tileId().text(), node->tileId().text() );
    QgsEventTracing::addEvent( QgsEventTracing::AsyncEnd, QStringLiteral( "3D" ), QStringLiteral( "Load" ), node->tileId().text() );
  }
  else
  {
    // return node back to loaded state
    node->cancelUpdating();

    QgsEventTracing::addEvent( QgsEventTracing::AsyncEnd, QStringLiteral( "3D" ), QStringLiteral( "Update" ), node->tileId().text() );
  }

  job->cancel();
  mActiveJobs.removeOne( job );
  job->deleteLater();
}

void QgsChunkedEntity::cancelActiveJobs()
{
  while ( !mActiveJobs.isEmpty() )
  {
    cancelActiveJob( mActiveJobs.takeFirst() );
  }
}


QVector<QgsRayCastingUtils::RayHit> QgsChunkedEntity::rayIntersection( const QgsRayCastingUtils::Ray3D &ray, const QgsRayCastingUtils::RayCastContext &context ) const
{
  Q_UNUSED( ray )
  Q_UNUSED( context )
  return QVector<QgsRayCastingUtils::RayHit>();
}

/// @endcond
