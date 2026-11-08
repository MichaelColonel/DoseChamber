/*
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include <QTimer>
#include <QApplication>
#include <QScopedPointer>

#include <TApplication.h>
#include <TSystem.h>

#include "mainwindow.h"

#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
#include <QtWebEngine>
#endif

int main(int argc, char **argv)
{
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
   // must be called before creating QApplication, from Qt 5.13, not needed for Qt6
   QtWebEngine::initialize();
#endif

   argc = 1; // hide all additional parameters from ROOT and Qt
   TApplication rootApp("Qt ROOT Application", &argc, argv); // ROOT application

   char* argv2[3] = {};
   argv2[0] = argv[0];
   argv2[1] = 0;

   QApplication qtApp(argc, argv2); // Qt application

   // let run ROOT event loop
   QScopedPointer< QTimer > timer(new QTimer());
   QObject::connect(timer.data(), &QTimer::timeout, [](){ gSystem->ProcessEvents(); });
   timer->setSingleShot(false);
   timer->start(20);

   // main window
   QScopedPointer< MainWindow > window(new MainWindow());
   window->show();

   window->setWindowTitle(QString("DoseChamber ") + QT_VERSION_STR);

   QObject::connect(&qtApp, SIGNAL(lastWindowClosed()), &qtApp, SLOT(quit()));

   int res = qtApp.exec();
   timer->stop();

   return res;
}
