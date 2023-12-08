/***************************************************************************
                         qgslogger.cpp  -  description
                             -------------------
    begin                : April 2006
    copyright            : (C) 2006 by Marco Hugentobler
    email                : marco.hugentobler at karto dot baug dot ethz dot ch
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgslogger.h"

#include <QApplication>
#include <QtDebug>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QDateTime>
#include <QMessageLogger>
#include <QMutex>
#ifndef CMAKE_SOURCE_DIR
#error CMAKE_SOURCE_DIR undefined
#endif // CMAKE_SOURCE_DIR

#ifdef WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

#ifdef HAVE_CRASH_HANDLER
#  if defined(__GLIBC__) || defined(__FreeBSD__)
#    define QGIS_CRASH
#    include <unistd.h>
#    include <execinfo.h>
#    include <csignal>
#    include <sys/wait.h>
#    include <cerrno>
#  endif
#endif

int QgsLogger::sDebugLevel = -999; // undefined value
int QgsLogger::sPrefixLength = -1;
Q_GLOBAL_STATIC( QString, sFileFilter )
Q_GLOBAL_STATIC( QString, sLogFile )
Q_GLOBAL_STATIC( QString, sLogFormat )
Q_GLOBAL_STATIC( QElapsedTimer, sTime )

void myPrint( const char *fmt, ... )
{
  va_list ap;
  va_start( ap, fmt );
#if defined(Q_OS_WIN)
  char buffer[1024];
  vsnprintf( buffer, sizeof buffer, fmt, ap );
  OutputDebugString( buffer );
#else
  vfprintf( stderr, fmt, ap );
#endif
  va_end( ap );
}

void QgsLogger::dumpBacktrace( unsigned int depth )
{
  if ( depth == 0 )
    depth = 20;

#ifdef QGIS_CRASH
  // Below there is a bunch of operations that are not safe in multi-threaded
  // environment (dup()+close() combo, wait(), juggling with file descriptors).
  // Maybe some problems could be resolved with dup2() and waitpid(), but it seems
  // that if the operations on descriptors are not serialized, things will get nasty.
  // That's why there's this lovely mutex here...
  static QMutex sMutex;
  QMutexLocker locker( &sMutex );

  int stderr_fd = -1;
  if ( access( "/usr/bin/c++filt", X_OK ) < 0 )
  {
    myPrint( "Stacktrace (c++filt NOT FOUND):\n" );
  }
  else
  {
    int fd[2];

    if ( pipe( fd ) == 0 && fork() == 0 )
    {
      close( STDIN_FILENO ); // close stdin

      // stdin from pipe
      if ( dup( fd[0] ) != STDIN_FILENO )
      {
        QgsDebugError( QStringLiteral( "dup to stdin failed" ) );
      }

      close( fd[1] );        // close writing end
      execl( "/usr/bin/c++filt", "c++filt", static_cast< char * >( nullptr ) );
      perror( "could not start c++filt" );
      exit( 1 );
    }

    myPrint( "Stacktrace (piped through c++filt):\n" );
    stderr_fd = dup( STDERR_FILENO );
    close( fd[0] );          // close reading end
    close( STDERR_FILENO );  // close stderr

    // stderr to pipe
    int stderr_new = dup( fd[1] );
    if ( stderr_new != STDERR_FILENO )
    {
      if ( stderr_new >= 0 )
        close( stderr_new );
      QgsDebugError( QStringLiteral( "dup to stderr failed" ) );
    }

    close( fd[1] );  // close duped pipe
  }

  void **buffer = new void *[ depth ];
  int nptrs = backtrace( buffer, depth );
  backtrace_symbols_fd( buffer, nptrs, STDERR_FILENO );
  delete [] buffer;
  if ( stderr_fd >= 0 )
  {
    int status;
    close( STDERR_FILENO );
    int dup_stderr = dup( stderr_fd );
    if ( dup_stderr != STDERR_FILENO )
    {
      close( dup_stderr );
      QgsDebugError( QStringLiteral( "dup to stderr failed" ) );
    }
    close( stderr_fd );
    wait( &status );
  }
#elif defined(Q_OS_WIN)
  // TODO Replace with incoming QgsStackTrace
#else
  Q_UNUSED( depth )
#endif
}

void QgsLogger::qgisCrash( int signal )
{
#ifdef QGIS_CRASH
  fprintf( stderr, "QGIS died on signal %d", signal );

  if ( access( "/usr/bin/gdb", X_OK ) == 0 )
  {
    // take full stacktrace using gdb
    // http://stackoverflow.com/questions/3151779/how-its-better-to-invoke-gdb-from-program-to-print-its-stacktrace
    // unfortunately, this is not so simple. the proper method is way more OS-specific
    // than this code would suggest, see http://stackoverflow.com/a/1024937

    char exename[512];
#if defined(__FreeBSD__)
    int len = readlink( "/proc/curproc/file", exename, sizeof( exename ) - 1 );
#else
    int len = readlink( "/proc/self/exe", exename, sizeof( exename ) - 1 );
#endif
    if ( len < 0 )
    {
      myPrint( "Could not read link (%d: %s)\n", errno, strerror( errno ) );
    }
    else
    {
      exename[ len ] = 0;

      char pidstr[32];
      snprintf( pidstr, sizeof pidstr, "--pid=%d", getpid() );

      int gdbpid = fork();
      if ( gdbpid == 0 )
      {
        // attach, backtrace and continue
        execl( "/usr/bin/gdb", "gdb", "-q", "-batch", "-n", pidstr, "-ex", "thread", "-ex", "bt full", exename, NULL );
        perror( "cannot exec gdb" );
        exit( 1 );
      }
      else if ( gdbpid >= 0 )
      {
        int status;
        waitpid( gdbpid, &status, 0 );
        myPrint( "gdb returned %d\n", status );
      }
      else
      {
        myPrint( "Cannot fork (%d: %s)\n", errno, strerror( errno ) );
        QgsLogger::dumpBacktrace( 256 );
      }
    }
  }

  abort();
#else
  Q_UNUSED( signal )
#endif
}

/*
 * Hook into the qWarning/qFatal mechanism so that we can channel messages
 * from libpng to the user.
 *
 * Some JPL WMS images tend to overload the libpng 1.2.2 implementation
 * somehow (especially when zoomed in)
 * and it would be useful for the user to know why their picture turned up blank
 *
 * Based on qInstallMsgHandler example code in the Qt documentation.
 *
 */
void QgsLogger::qtMessageHandler( QtMsgType type, const QMessageLogContext &context, const QString &msg )
{
//    static QMutex mutex;
//    QMutexLocker lock(&mutex);

  QString formatedMessage = qPrintable( qFormatLogMessage( type, context, msg ) );
  if ( type == QtFatalMsg )
  {
    std::cerr << formatedMessage.toUtf8().constData() << std::endl;
#ifndef QGIS_CRASH
    QgsLogger::dumpBacktrace( 256 );
#endif
  }
  else if ( type == QtCriticalMsg )
  {
    std::cerr << formatedMessage.toUtf8().constData() << std::endl;
#ifdef QGISDEBUG
    QgsLogger::dumpBacktrace( 20 );
#endif
  }
  else if ( type == QtWarningMsg )
  {
    /* Ignore:
     * - libpng iCPP known incorrect SRGB profile errors (which are thrown by 3rd party components
     *  we have no control over and have low value anyway);
     * - QtSVG warnings with regards to lack of implementation beyond Tiny SVG 1.2
     */
    if ( msg.contains( QLatin1String( "QXcbClipboard" ), Qt::CaseInsensitive ) ||
         msg.contains( QLatin1String( "QGestureManager::deliverEvent" ), Qt::CaseInsensitive ) ||
         msg.startsWith( QLatin1String( "libpng warning: iCCP: known incorrect sRGB profile" ), Qt::CaseInsensitive ) ||
         msg.contains( QLatin1String( "Could not add child element to parent element because the types are incorrect" ), Qt::CaseInsensitive ) ||
         msg.contains( QLatin1String( "OpenType support missing for" ), Qt::CaseInsensitive ) )
      return;

    std::cout << formatedMessage.toUtf8().constData() << std::endl;

#ifdef QGISDEBUG
    // Print all warnings except setNamedColor.
    // Only seems to happen on windows
    if ( !msg.startsWith( QLatin1String( "QColor::setNamedColor: Unknown color name 'param" ), Qt::CaseInsensitive )
         && !msg.startsWith( QLatin1String( "Trying to create a QVariant instance of QMetaType::Void type, an invalid QVariant will be constructed instead" ), Qt::CaseInsensitive )
         && !msg.startsWith( QLatin1String( "Logged warning" ), Qt::CaseInsensitive ) )
    {
      // TODO: Verify this code in action.
      QgsLogger::dumpBacktrace( 20 );

      // TO RE-ENABLE?
      /*
      // also be super obnoxious -- we DON'T want to allow these errors to be ignored!!
      if ( QgisApp::instance() && QgisApp::instance()->messageBar() && QgisApp::instance()->thread() == QThread::currentThread() )
      {
        QgisApp::instance()->messageBar()->pushCritical( QStringLiteral( "Qt" ), msg );
      }
      else
      {
        QgsMessageLog::logMessage( msg, QStringLiteral( "Qt" ) );
      }*/
    }
#endif

    // TODO: Verify this code in action.
    if ( msg.startsWith( QLatin1String( "libpng error:" ), Qt::CaseInsensitive ) )
    {
      // Let the user know
      // // TO RE-ENABLE? QgsMessageLog::logMessage( msg, QStringLiteral( "libpng" ) );
    }
  }
  else
  {
    std::cout << formatedMessage.toUtf8().constData() << std::endl;
    if ( msg.startsWith( QLatin1String( "Backtrace" ) ) )
    {
      const QString trace = msg.mid( 9 );
      QgsLogger::dumpBacktrace( atoi( trace.toLocal8Bit().constData() ) );
    }
  }

  // at last log to file if needed
  if ( !sLogFile()->isEmpty() )
  {
    static QFile logFile( *sLogFile() );
    static bool logFileIsOpen = logFile.open( QIODevice::Append | QIODevice::Text );

    if ( logFileIsOpen )
    {
      logFile.write( formatedMessage.toUtf8() + '\n' );
      logFile.flush();
    }
  }

  // final action for fatal message
  if ( type == QtFatalMsg )
  {
#ifdef QGIS_CRASH
    QgsLogger::qgisCrash( -1 );
#else
    abort();                    // deliberately dump core
#endif
  }
}

void QgsLogger::init()
{
  static QMutex mutex;
  QMutexLocker lock( &mutex );
  if ( sDebugLevel != -999 )
    return;

  sTime()->start();

  *sLogFormat() = getenv( "QGIS_LOG_FORMAT" )
                  ? getenv( "QGIS_LOG_FORMAT" )
                  : ( getenv( "QT_MESSAGE_PATTERN" )
                      ? getenv( "QT_MESSAGE_PATTERN" )
                      : LOG_FORMAT_LEGACY );
  if ( *sLogFormat() == "PRETTY" )
    *sLogFormat() = LOG_FORMAT_PRETTY;
  else if ( *sLogFormat() == "PIPED" )
    *sLogFormat() = LOG_FORMAT_PIPED;

  *sLogFile() = getenv( "QGIS_LOG_FILE" ) ? getenv( "QGIS_LOG_FILE" ) : "";
  *sFileFilter() = getenv( "QGIS_DEBUG_FILE" ) ? getenv( "QGIS_DEBUG_FILE" ) : "";
  sDebugLevel = getenv( "QGIS_DEBUG" ) ? atoi( getenv( "QGIS_DEBUG" ) ) :
#ifdef QGISDEBUG
                1
#else
                0
#endif
                ;

  sPrefixLength = sizeof( CMAKE_SOURCE_DIR );
  // cppcheck-suppress internalAstError
  if ( CMAKE_SOURCE_DIR[sPrefixLength - 1] == '/' )
    sPrefixLength++;

  qSetMessagePattern( *sLogFormat() );
#ifndef ANDROID
  qInstallMessageHandler( QgsLogger::qtMessageHandler );
#endif
}

void QgsLogger::debug( const QString &msg, int debuglevel, const char *file, const char *function, int line )
{
  init();

  if ( !file && !sFileFilter()->isEmpty() && !sFileFilter()->endsWith( file ) )
    return;

  if ( debuglevel != QGS_LOG_LVL_WARNING && debuglevel > sDebugLevel )
    return;


  QString m = msg;

  // compute file/function/line data
  QString funcStr;
  if ( function )
  {
    funcStr = function;
  }
  else
  {
    funcStr = "nofunc";
  }

  QString fileStr;
  if ( file )
  {
    // compute elapsed time
    m.prepend( QStringLiteral( "[%1ms] " ).arg( sTime()->elapsed() ) );
    sTime()->restart();

#ifndef _MSC_VER
    fileStr = file + ( file[0] == '/' ? sPrefixLength : 0 );
#else
    fileStr = file;
#endif
  }
  else
  {
    fileStr = "nofile";
  }

  // compute log level
  if ( debuglevel == QGS_LOG_LVL_FATAL )
    QMessageLogger( fileStr.toUtf8().constData(), line, funcStr.toUtf8().constData() ).fatal( "%s", m.toUtf8().constData() );
  else if ( debuglevel == QGS_LOG_LVL_CRITICAL )
    QMessageLogger( fileStr.toUtf8().constData(), line, funcStr.toUtf8().constData() ).critical( "%s", m.toUtf8().constData() );
  else if ( debuglevel == QGS_LOG_LVL_WARNING )
    QMessageLogger( fileStr.toUtf8().constData(), line, funcStr.toUtf8().constData() ).warning( "%s", m.toUtf8().constData() );
  else if ( debuglevel == QGS_LOG_LVL_INFO )
    QMessageLogger( fileStr.toUtf8().constData(), line, funcStr.toUtf8().constData() ).info( "%s", m.toUtf8().constData() );
  else if ( debuglevel == QGS_LOG_LVL_DEBUG )
  {
    QMessageLogger( fileStr.toUtf8().constData(), line, funcStr.toUtf8().constData() ).debug( "%s", m.toUtf8().constData() );
  }
  else
  {
    QString level = QString( "<dbgLvl:%1> " ).arg( debuglevel );
    m.prepend( level );
    QMessageLogger( fileStr.toUtf8().constData(), line, funcStr.toUtf8().constData() ).debug( "%s", m.toUtf8().constData() );
  }
}

void QgsLogger::debug( const QString &var, int val, int debuglevel, const char *file, const char *function, int line )
{
  debug( QStringLiteral( "%1: %2" ).arg( var ).arg( val ), debuglevel, file, function, line );
}

void QgsLogger::debug( const QString &var, double val, int debuglevel, const char *file, const char *function, int line )
{
  debug( QStringLiteral( "%1: %2" ).arg( var ).arg( val ), debuglevel, file, function, line );
}

void QgsLogger::info( const QString &msg )
{
  if ( sDebugLevel == -999 )
    init();
  qInfo( "%s", msg.toLocal8Bit().constData() );
}

void QgsLogger::warning( const QString &msg )
{
  if ( sDebugLevel == -999 )
    init();
  qWarning( "%s", msg.toLocal8Bit().constData() );
}

void QgsLogger::critical( const QString &msg )
{
  if ( sDebugLevel == -999 )
    init();
  qCritical( "%s", msg.toLocal8Bit().constData() );
}

void QgsLogger::fatal( const QString &msg )
{
  if ( sDebugLevel == -999 )
    init();
  qFatal( "%s", msg.toLocal8Bit().constData() );
}

QString QgsLogger::logFile()
{
  if ( sDebugLevel == -999 )
    init();
  return *sLogFile();
}

QString QgsLogger::logFormat()
{
  if ( sDebugLevel == -999 )
    init();
  return *sLogFormat();
}
