/***************************************************************************
                         qgslogger.h  -  description
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

#ifndef QGSLOGGER_H
#define QGSLOGGER_H

#include <iostream>
#include "qgis_sip.h"
#include <sstream>
#include <QString>
#include <QTime>

#include "qgis_core.h"
#include "qgsconfig.h"

class QFile;

// There was no defined value to log WARNING or FATAL message. To preserve the current code base using QgsDebugMsgLevel
// the WARNING value can not be in the good order. This induces a choice: if QGIS_DEBUG is 0 (ie. critical),
// we will also display the WARNING messages.
// TODO: enum for python users?
#define QGS_LOG_LVL_FATAL -1
#define QGS_LOG_LVL_CRITICAL 0
#define QGS_LOG_LVL_WARNING 500
#define QGS_LOG_LVL_INFO 1
#define QGS_LOG_LVL_DEBUG 2
#define QGS_LOG_LVL_TRACE 3

#ifdef QGISDEBUG
#define QgsDebugError(str) QgsLogger::debug(QString(str), QGS_LOG_LVL_CRITICAL, __FILE__, __FUNCTION__, __LINE__)
#define QgsDebugMsgLevel(str, level) if ( level <= QgsLogger::debugLevel() || level == QGS_LOG_LVL_WARNING ) { QgsLogger::debug(QString(str), (level), __FILE__, __FUNCTION__, __LINE__); }(void)(0)
#define QgsDebugCall QgsScopeLogger _qgsScopeLogger(__FILE__, __FUNCTION__, __LINE__)
#else
#define QgsDebugCall do {} while(false)
#define QgsDebugError(str) do {} while(false)
#define QgsDebugMsgLevel(str, level) do {} while(false)
#endif

#define LOG_FORMAT_LEGACY "%{file}:%{line} : (%{function}) [thread:%{qthreadptr}] %{message}"
#define LOG_FORMAT_PRETTY "%{time yyyy-MM-ddThh:mm:ss.zzz} [%{qthreadptr}] %{if-debug}DEBUG%{endif}%{if-info}INFO %{endif}%{if-warning}WARN %{endif}%{if-critical}ERROR%{endif}%{if-fatal}FATAL%{endif} %{file}:%{line}(%{function}) - %{message}"
#define LOG_FORMAT_PIPED "%{time yyyy-MM-ddThh:mm:ss.zzz} | %{if-debug}DEBUG%{endif}%{if-info}INFO %{endif}%{if-warning}WARN %{endif}%{if-critical}ERROR%{endif}%{if-fatal}FATAL%{endif} | %{qthreadptr} | %{function}(%{file}:%{line}) | %{message}"

/**
 * \ingroup core
 * \brief QgsLogger is a class to print debug/warning/error messages to the console.
 *
 * The advantage of this class over iostream & co. is that the
 * output can be controlled with environment variables:
 * QGIS_DEBUG is an int describing what debug messages are written to the console.
 * If the debug level of a message is <= QGIS_DEBUG, the message is written to the
 * console. It the variable QGIS_DEBUG is not defined, it defaults to 1 for debug
 * mode and to 0 for release mode
 * QGIS_DEBUG_FILE may contain a file name. Only the messages from this file are
 * printed (provided they have the right debuglevel). If QGIS_DEBUG_FILE is not
 * set, messages from all files are printed
 *
 * QGIS_LOG_FILE may contain a file name. If set, all messages will be appended
 * to this file rather than to stdout.
*/
class CORE_EXPORT QgsLogger
{
  public:

    // TODO: With c++20 we could use [source_location](https://en.cppreference.com/w/cpp/utility/source_location)
    // to retrieve file, line and function params for warning, critical, etc. functions.

    // TODO: should be renammed as logMessage (for example) and add a new function to do debug as info, warning, critical, etc.

    /**
     * Goes to qDebug.
     * \param msg the message to be printed
     * \param debuglevel
     * \param file file name where the message comes from
     * \param function function where the message comes from
     * \param line place in file where the message comes from
    */
    static void debug( const QString &msg, int debuglevel = QGS_LOG_LVL_DEBUG, const char *file = nullptr, const char *function = nullptr, int line = -1 );

    //! Similar to the previous method, but prints a variable int-value pair
    static void debug( const QString &var, int val, int debuglevel = QGS_LOG_LVL_DEBUG, const char *file = nullptr, const char *function = nullptr, int line = -1 );

    /**
     * Similar to the previous method, but prints a variable double-value pair
     * \note not available in Python bindings
     */
    static void debug( const QString &var, double val, int debuglevel = QGS_LOG_LVL_DEBUG, const char *file = nullptr, const char *function = nullptr, int line = -1 ) SIP_SKIP SIP_SKIP;

    /**
     * Prints out a variable/value pair for types with overloaded operator<<
     * \note not available in Python bindings
     */
    template <typename T> static void debug( const QString &var, T val, const char *file = nullptr, const char *function = nullptr,
        int line = -1, int debuglevel = QGS_LOG_LVL_DEBUG ) SIP_SKIP SIP_SKIP
    {
      std::ostringstream os;
      os << var.toLocal8Bit().data() << " = " << val;
      debug( var, os.str().c_str(), file, function, line, debuglevel );
    }

    //! Goes to qInfo.
    static void info( const QString &msg );

    //! Goes to qWarning.
    static void warning( const QString &msg );

    //! Goes to qCritical.
    static void critical( const QString &msg );

    //! Goes to qFatal.
    static void fatal( const QString &msg );

    /**
     * Tries to dump stack trace through c++filt
     */
    static void dumpBacktrace( unsigned int depth ) SIP_SKIP;

    /**
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
    static void qtMessageHandler( QtMsgType type, const QMessageLogContext &context, const QString &msg ) SIP_SKIP;

    //! Handle qgis crash
    static void qgisCrash( int signal ) SIP_SKIP;

    /**
     * Reads the environment variable QGIS_DEBUG and converts it to int. If QGIS_DEBUG is not set,
     * the function returns 1 if QGISDEBUG is defined and 0 if not.
    */
    static int debugLevel()
    {
      if ( sDebugLevel == -999 )
        init();
      return sDebugLevel;
    }

    /**
     * Reads the environment variable QGIS_LOG_FILE. Returns NULL if the variable is not set,
     * otherwise returns a file name for writing log messages to.
    */
    static QString logFile();

    /**
     * Returns the log format used by qgis.
    */
    static QString logFormat();

  private:
    static void init();

    //! Current debug level
    static int sDebugLevel;
    static int sPrefixLength;
};

/**
 * \ingroup core
 */
class CORE_EXPORT QgsScopeLogger // clazy:exclude=rule-of-three
{
  public:
    QgsScopeLogger( const char *file, const char *func, int line )
      : _file( file )
      , _func( func )
      , _line( line )
    {
      QgsLogger::debug( QStringLiteral( "Entering." ), QGS_LOG_LVL_DEBUG, _file, _func, _line );
    }
    ~QgsScopeLogger()
    {
      QgsLogger::debug( QStringLiteral( "Leaving." ), QGS_LOG_LVL_DEBUG, _file, _func, _line );
    }
  private:
    const char *_file = nullptr;
    const char *_func = nullptr;
    int _line;
};

#endif
