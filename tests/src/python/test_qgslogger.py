"""QGIS Unit tests for QgsLogger.

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""
__author__ = 'Tim Sutton'
__date__ = '20/08/2012'
__copyright__ = 'Copyright 2012, The QGIS Project'

import os
import tempfile


(myFileHandle, myFilename) = tempfile.mkstemp()
os.environ['QGIS_DEBUG'] = '2'
os.environ['QGIS_LOG_FILE'] = myFilename

from qgis.core import QgsLogger
from qgis.testing import unittest

# Convenience instances in case you may need them
# not used in this test
# from qgis.testing import start_app
# start_app()


class TestQgsLogger(unittest.TestCase):

    def testLogger(self):
        try:
            os.environ["QGIS_LOG_FORMAT"] = "%{type}: %{message}"
            myFile = os.fdopen(myFileHandle, "w")
            myFile.write("QGIS Logger Unit Test\n")
            myFile.close()
            myLogger = QgsLogger()
            # debug has 'info' type due to sip bug in reading default value for `debuglevel`. We force it temporarily.
            myLogger.debug('This is a debug', 2)
            myLogger.info('This is a info')
            myLogger.warning('This is a warning')
            myLogger.critical('This is critical')
            # myLogger.fatal('Aaaargh...fatal');  #kills QGIS not testable
            myFile = open(myFilename)
            myText = myFile.readlines()
            myFile.close()
            myExpectedText = ['QGIS Logger Unit Test\n',
                              'debug: This is a debug\n',
                              'info: This is a info\n',
                              'warning: This is a warning\n',
                              'critical: This is critical\n']
            myMessage = ('Expected:\n---\n%s\n---\nGot:\n---\n%s\n---\n' %
                         (myExpectedText, myText))
            self.assertEqual(myText, myExpectedText, myMessage)
        finally:
            pass
            os.remove(myFilename)


if __name__ == '__main__':
    unittest.main()
