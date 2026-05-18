/***************************************************************************
  qgsvectorlayer3drendererwidget.cpp
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

#include "qgsvectorlayer3drendererwidget.h"

#include "qgisapp.h"
#include "qgs3dsymbolregistry.h"
#include "qgs3dsymbolutils.h"
#include "qgsapplication.h"
#include "qgscategorized3drenderer.h"
#include "qgscategorized3drendererwidget.h"
#include "qgscategorizedsymbolrenderer.h"
#include "qgsmapcanvas.h"
#include "qgsrulebased3drenderer.h"
#include "qgsrulebased3drendererwidget.h"
#include "qgsrulebasedrenderer.h"
#include "qgssinglesymbolrenderer.h"
#include "qgssymbol3dwidget.h"
#include "qgsvectorlayer.h"
#include "qgsvectorlayer3dpropertieswidget.h"
#include "qgsvectorlayer3drenderer.h"
#include "qgsvscrollarea.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QString>

#include "moc_qgsvectorlayer3drendererwidget.cpp"

using namespace Qt::StringLiterals;

QgsSingleSymbol3DRendererWidget::QgsSingleSymbol3DRendererWidget( QgsVectorLayer *layer, QWidget *parent )
  : QWidget( parent )
  , mLayer( layer )
{
  // If layer is null, the widget cannot be created.
  Q_ASSERT( mLayer );

  QVBoxLayout *scrollLayout = new QVBoxLayout();
  scrollLayout->setContentsMargins( 0, 0, 0, 0 );

  QgsVScrollArea *scrollArea = new QgsVScrollArea( this );
  scrollArea->setFrameShape( QFrame::NoFrame );
  scrollArea->setFrameShadow( QFrame::Plain );
  scrollArea->setWidgetResizable( true );
  scrollLayout->addWidget( scrollArea );

  widgetSymbol = new QgsSymbol3DWidget( mLayer, this );
  scrollArea->setWidget( widgetSymbol );

  setLayout( scrollLayout );

  connect( widgetSymbol, &QgsSymbol3DWidget::widgetChanged, this, &QgsSingleSymbol3DRendererWidget::widgetChanged );
}


void QgsSingleSymbol3DRendererWidget::setLayer( QgsVectorLayer *layer )
{
  // If layer is null, the widget cannot be updated.
  Q_ASSERT( layer );

  mLayer = layer;

  QgsAbstract3DRenderer *r = mLayer->renderer3D();
  bool rendererSet = false;
  if ( r && r->type() == "vector"_L1 )
  {
    QgsVectorLayer3DRenderer *vectorRenderer = static_cast<QgsVectorLayer3DRenderer *>( r );
    widgetSymbol->setSymbol( vectorRenderer->symbol(), mLayer );
    rendererSet = true;
  }
  else if ( QgsAbstractVectorLayer3DRenderer *vectorRenderer = dynamic_cast< QgsAbstractVectorLayer3DRenderer * >( r ) )
  {
    std::unique_ptr< QgsVectorLayer3DRenderer > newRenderer = QgsVectorLayer3DRenderer::convertFromRenderer( vectorRenderer );
    if ( newRenderer )
    {
      widgetSymbol->setSymbol( newRenderer->symbol(), mLayer );
      rendererSet = true;
    }
  }
  if ( !rendererSet )
  {
    const std::unique_ptr<QgsAbstract3DSymbol> sym( QgsApplication::symbol3DRegistry()->defaultSymbolForGeometryType( mLayer->geometryType() ) );
    sym->setDefaultPropertiesFromLayer( mLayer );
    widgetSymbol->setSymbol( sym.get(), mLayer );
  }
}

std::unique_ptr<QgsAbstract3DSymbol> QgsSingleSymbol3DRendererWidget::symbol()
{
  return widgetSymbol->symbol(); // cloned or null
}

// -------

QgsVectorLayer3DRendererWidget::QgsVectorLayer3DRendererWidget( QgsMapLayer *layer, QgsMapCanvas *canvas, QWidget *parent )
  : QgsMapLayerConfigWidget( layer, canvas, parent )
{
  setPanelTitle( tr( "3D View" ) );
  setObjectName( u"mOptsPage_3DView"_s );

  QVBoxLayout *layout = new QVBoxLayout( this );
  layout->setContentsMargins( 0, 0, 0, 0 );

  cboRendererType = new QComboBox( this );
  cboRendererType->addItem( QgsApplication::getThemeIcon( u"mIconRenderOnTerrain.svg"_s ), tr( "Render on Terrain Surface" ) );
  cboRendererType->addItem( QgsApplication::getThemeIcon( u"rendererSingleSymbol.svg"_s ), tr( "Single Symbol" ) );
  cboRendererType->addItem( QgsApplication::getThemeIcon( u"rendererCategorizedSymbol.svg"_s ), tr( "Categorized" ) );
  cboRendererType->addItem( QgsApplication::getThemeIcon( u"rendererRuleBasedSymbol.svg"_s ), tr( "Rule-based" ) );
  cboRendererType->addItem( QgsApplication::getThemeIcon( u"propertyicons/symbology.svg"_s ), tr( "From 2D Symbology" ) );

  widgetBaseProperties = new QgsVectorLayer3DPropertiesWidget( this );

  widgetRendererStack = new QStackedWidget( this );
  layout->addWidget( cboRendererType );
  layout->addWidget( widgetRendererStack );
  layout->addWidget( widgetBaseProperties );

  widgetNoRenderer = new QLabel;
  widgetSingleSymbolRenderer = new QgsSingleSymbol3DRendererWidget( qobject_cast<QgsVectorLayer *>( layer ), this );
  widgetCategorizedRenderer = new QgsCategorized3DRendererWidget( this );
  widgetRuleBasedRenderer = new QgsRuleBased3DRendererWidget( this );

  widgetRendererStack->addWidget( widgetNoRenderer );
  widgetRendererStack->addWidget( widgetSingleSymbolRenderer );
  widgetRendererStack->addWidget( widgetCategorizedRenderer );
  widgetRendererStack->addWidget( widgetRuleBasedRenderer );

  connect( cboRendererType, qOverload<int>( &QComboBox::currentIndexChanged ), this, &QgsVectorLayer3DRendererWidget::onRendererTypeChanged );
  connect( widgetSingleSymbolRenderer, &QgsSingleSymbol3DRendererWidget::widgetChanged, this, &QgsVectorLayer3DRendererWidget::widgetChanged );
  connect( widgetCategorizedRenderer, &QgsCategorized3DRendererWidget::widgetChanged, this, &QgsVectorLayer3DRendererWidget::widgetChanged );
  connect( widgetCategorizedRenderer, &QgsCategorized3DRendererWidget::showPanel, this, &QgsPanelWidget::openPanel );
  connect( widgetRuleBasedRenderer, &QgsRuleBased3DRendererWidget::widgetChanged, this, &QgsVectorLayer3DRendererWidget::widgetChanged );
  connect( widgetRuleBasedRenderer, &QgsRuleBased3DRendererWidget::showPanel, this, &QgsPanelWidget::openPanel );
  connect( widgetBaseProperties, &QgsVectorLayer3DPropertiesWidget::changed, this, &QgsVectorLayer3DRendererWidget::widgetChanged );

  setProperty( "helpPage", u"working_with_vector/vector_properties.html#d-view-properties"_s );

  syncToLayer( layer );
}


void QgsVectorLayer3DRendererWidget::syncToLayer( QgsMapLayer *layer )
{
  QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
  if ( !vlayer || vlayer->geometryType() == Qgis::GeometryType::Null )
  {
    setEnabled( false ); // hide 3d symbology
    return;
  }
  mLayer = layer;

  // Enable the "From 2D Symbology" item only when the current
  // 2D renderer type supports conversion to a corresponding 3D renderer.
  const QgsVectorLayer *vLayer = qobject_cast<const QgsVectorLayer *>( mLayer );
  const QgsFeatureRenderer *renderer2D = vLayer->renderer();
  const QString renderer2DType = renderer2D ? renderer2D->type() : "";
  QStandardItemModel *rendererTypeModel = qobject_cast<QStandardItemModel *>( cboRendererType->model() );
  QStandardItem *from2DItem = rendererTypeModel->item( 4 );
  if ( renderer2DType == "singleSymbol"_L1 || renderer2DType == "RuleRenderer"_L1 || renderer2DType == "categorizedSymbol"_L1 )
  {
    from2DItem->setFlags( from2DItem->flags() | Qt::ItemIsEnabled );
  }
  else
  {
    from2DItem->setFlags( from2DItem->flags() & ~Qt::ItemIsEnabled );
  }

  int pageIndex;
  QgsAbstract3DRenderer *r = vlayer->renderer3D();
  if ( r && r->type() == "vector"_L1 )
  {
    pageIndex = 1;
    widgetSingleSymbolRenderer->setLayer( vlayer );
  }
  else if ( r && r->type() == "categorized"_L1 )
  {
    pageIndex = 2;
    widgetCategorizedRenderer->setLayer( vlayer );
  }
  else if ( r && r->type() == "rulebased"_L1 )
  {
    pageIndex = 3;
    widgetRuleBasedRenderer->setLayer( vlayer );
  }
  else
  {
    pageIndex = 0;
  }
  widgetRendererStack->setCurrentIndex( pageIndex );
  whileBlocking( cboRendererType )->setCurrentIndex( pageIndex );

  if ( r && ( QStringList { u"vector"_s, u"rulebased"_s, u"categorized"_s }.contains( r->type() ) ) )
  {
    widgetBaseProperties->load( static_cast<QgsAbstractVectorLayer3DRenderer *>( r ) );
  }
  else
  {
    widgetBaseProperties->reset();
  }
}

void QgsVectorLayer3DRendererWidget::setDockMode( bool dockMode )
{
  QgsPanelWidget::setDockMode( dockMode );
  widgetRuleBasedRenderer->setDockMode( dockMode );
  widgetCategorizedRenderer->setDockMode( dockMode );
}


void QgsVectorLayer3DRendererWidget::apply()
{
  const int idx = widgetRendererStack->currentIndex();
  switch ( idx )
  {
    case 0:
      mLayer->setRenderer3D( nullptr );
      break;
    case 1:
    {
      std::unique_ptr<QgsAbstract3DSymbol> symbol = widgetSingleSymbolRenderer->symbol();
      QgsVectorLayer3DRenderer *r = new QgsVectorLayer3DRenderer( symbol ? symbol.release() : nullptr );
      r->setLayer( qobject_cast<QgsVectorLayer *>( mLayer ) );
      widgetBaseProperties->apply( r );
      mLayer->setRenderer3D( r );
    }
    break;
    case 2:
    {
      std::unique_ptr<QgsCategorized3DRenderer> renderer( widgetCategorizedRenderer->renderer()->clone() );
      renderer->setLayer( qobject_cast<QgsVectorLayer *>( mLayer ) );
      widgetBaseProperties->apply( renderer.get() );
      mLayer->setRenderer3D( renderer.release() );
    }
    break;
    case 3:
    {
      QgsRuleBased3DRenderer *r = new QgsRuleBased3DRenderer( widgetRuleBasedRenderer->rootRule()->clone() );
      r->setLayer( qobject_cast<QgsVectorLayer *>( mLayer ) );
      widgetBaseProperties->apply( r );
      mLayer->setRenderer3D( r );
    }
    break;
    default:
      Q_ASSERT( false );
  }
}

void QgsVectorLayer3DRendererWidget::onRendererTypeChanged( int index )
{
  widgetRendererStack->setCurrentIndex( index );
  switch ( index )
  {
    case 0:
      break;
    case 1:
      widgetSingleSymbolRenderer->setLayer( qobject_cast<QgsVectorLayer *>( mLayer ) );
      break;
    case 2:
      widgetCategorizedRenderer->setLayer( qobject_cast<QgsVectorLayer *>( mLayer ) );
      break;
    case 3:
      widgetRuleBasedRenderer->setLayer( qobject_cast<QgsVectorLayer *>( mLayer ) );
      break;
    case 4:
      createRendererFrom2DSymbology();
      break;
    default:
      Q_ASSERT( false );
  }
  emit widgetChanged();
}

void QgsVectorLayer3DRendererWidget::createRendererFrom2DSymbology()
{
  QgsVectorLayer *vLayer = qobject_cast<QgsVectorLayer *>( mLayer );
  QgsFeatureRenderer *renderer2D = vLayer->renderer();
  if ( !renderer2D )
  {
    // fallback to single symbol renderer
    cboRendererType->setCurrentIndex( 1 );
    return;
  }

  if ( renderer2D->type() == "singleSymbol"_L1 )
  {
    QgsSingleSymbolRenderer *singleRenderer2D = dynamic_cast<QgsSingleSymbolRenderer *>( renderer2D );
    if ( singleRenderer2D )
    {
      QgsVectorLayer3DRenderer *renderer3D = new QgsVectorLayer3DRenderer();
      QgsRenderContext context = QgsRenderContext::fromMapSettings( QgisApp::instance()->mapCanvas()->mapSettings() );
      std::unique_ptr<QgsAbstract3DSymbol> symbol3D = Qgs3DSymbolUtils::create3DSymbolFrom2D( vLayer, singleRenderer2D->symbol(), context );
      renderer3D->setSymbol( symbol3D.release() );
      vLayer->setRenderer3D( renderer3D );
    }

    cboRendererType->setCurrentIndex( 1 );
  }
  else if ( renderer2D->type() == "categorizedSymbol"_L1 )
  {
    const QgsCategorizedSymbolRenderer *categorizedRenderer2D = dynamic_cast<const QgsCategorizedSymbolRenderer *>( renderer2D );
    if ( categorizedRenderer2D )
    {
      QgsRenderContext context = QgsRenderContext::fromMapSettings( QgisApp::instance()->mapCanvas()->mapSettings() );
      QList<Qgs3DRendererCategory> categories3D;
      for ( const QgsRendererCategory &category2D : categorizedRenderer2D->categories() )
      {
        std::unique_ptr<QgsAbstract3DSymbol> symbol3D = Qgs3DSymbolUtils::create3DSymbolFrom2D( vLayer, category2D.symbol(), context );
        Qgs3DRendererCategory category3D( category2D.value(), symbol3D.release(), category2D.renderState() );
        categories3D.append( category3D );
      }

      QgsCategorized3DRenderer *renderer3D = new QgsCategorized3DRenderer( categorizedRenderer2D->classAttribute(), categories3D );
      std::unique_ptr<QgsAbstract3DSymbol> sourceSymbol3D = Qgs3DSymbolUtils::create3DSymbolFrom2D( vLayer, categorizedRenderer2D->sourceSymbol(), context );
      renderer3D->setSourceSymbol( sourceSymbol3D.release() );
      if ( categorizedRenderer2D->sourceColorRamp() )
      {
        renderer3D->setSourceColorRamp( categorizedRenderer2D->sourceColorRamp()->clone() );
      }
      vLayer->setRenderer3D( renderer3D );
    }

    cboRendererType->setCurrentIndex( 2 );
  }
  else if ( renderer2D->type() == "RuleRenderer"_L1 )
  {
    const QgsRuleBasedRenderer *ruleRenderer2D = dynamic_cast<const QgsRuleBasedRenderer *>( renderer2D );
    if ( ruleRenderer2D )
    {
      QgsRenderContext context = QgsRenderContext::fromMapSettings( QgisApp::instance()->mapCanvas()->mapSettings() );
      auto rootRule3D = std::make_unique<QgsRuleBased3DRenderer::Rule>( nullptr );
      for ( const QgsRuleBasedRenderer::Rule *rule2D : ruleRenderer2D->rootRule()->children() )
      {
        std::unique_ptr<QgsAbstract3DSymbol> symbol3D = Qgs3DSymbolUtils::create3DSymbolFrom2D( vLayer, rule2D->symbol(), context );
        QgsRuleBased3DRenderer::Rule *rule3D = new QgsRuleBased3DRenderer::Rule( symbol3D.release(), rule2D->filterExpression(), rule2D->label(), rule2D->isElse() );
        rootRule3D->appendChild( rule3D );
      }

      QgsRuleBased3DRenderer *renderer3D = new QgsRuleBased3DRenderer( rootRule3D.release() );
      vLayer->setRenderer3D( renderer3D );
    }

    cboRendererType->setCurrentIndex( 3 );
  }
  else
  {
    // fallback to single symbol renderer
    cboRendererType->setCurrentIndex( 1 );
    return;
  }
}

QgsVectorLayer3DRendererWidgetFactory::QgsVectorLayer3DRendererWidgetFactory( QObject *parent )
  : QObject( parent )
{
  setIcon( QIcon( ":/images/themes/default/3d.svg" ) );
  setTitle( tr( "3D View" ) );
}

QgsMapLayerConfigWidget *QgsVectorLayer3DRendererWidgetFactory::createWidget( QgsMapLayer *layer, QgsMapCanvas *canvas, bool dockWidget, QWidget *parent ) const
{
  Q_UNUSED( dockWidget )
  return new QgsVectorLayer3DRendererWidget( layer, canvas, parent );
}

bool QgsVectorLayer3DRendererWidgetFactory::supportLayerPropertiesDialog() const
{
  return true;
}

bool QgsVectorLayer3DRendererWidgetFactory::supportsLayer( QgsMapLayer *layer ) const
{
  return layer->type() == Qgis::LayerType::Vector;
}

QString QgsVectorLayer3DRendererWidgetFactory::layerPropertiesPagePositionHint() const
{
  return u"mOptsPage_Diagrams"_s;
}
