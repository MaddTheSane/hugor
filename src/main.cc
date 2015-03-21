#include <QDebug>
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QMessageBox>
#include <cstdlib>

extern "C" {
#include "heheader.h"
}
#include "hugodefs.h"
#include "version.h"
#include "happlication.h"
#include "settings.h"


// On some platforms, SDL redefines main in order to provide a
// platform-specific main() implementation.  However, Qt handles this too,
// so things can get weird.  We need to make sure main is not redefined so
// that Qt can find our own implementation and SDL will not try to do
// platform-specific initialization work (like launching the Cocoa event-loop
// or setting up the application menu on OS X, or redirecting stdout and stderr
// to text files on Windows), which would break things.
#ifdef main
#  undef main
#endif

// Static OS X builds need the Qt codec plugins.
#ifndef NO_STATIC_TEXTCODEC_PLUGINS
#  if defined(Q_OS_MAC)
#    include <QtPlugin>
     Q_IMPORT_PLUGIN(qcncodecs)
     Q_IMPORT_PLUGIN(qjpcodecs)
     Q_IMPORT_PLUGIN(qtwcodecs)
     Q_IMPORT_PLUGIN(qkrcodecs)
#  endif
#endif


int main( int argc, char* argv[] )
{
    initSoundEngine();
#ifndef DISABLE_VIDEO
    initVideoEngine(argc, argv);
#endif

    HApplication* app = new HApplication(argc, argv, "Hugor", HUGOR_VERSION,
                                         "Nikos Chantziaras", "");

    // Filename of the game to run.
    QString gameFileName;

    // Check if a game file with the same basename as ours exists in our
    // directory.  If yes, we will run it.
    gameFileName = app->applicationDirPath();
    if (not gameFileName.endsWith('/')) {
        gameFileName += '/';
    }
    gameFileName += QFileInfo(app->applicationFilePath()).baseName();
    gameFileName += ".hex";
    if (not QFileInfo(gameFileName).exists()) {
        gameFileName.clear();
    }

    const QStringList& args = app->arguments();
    if (args.size() == 2) {
        if (QFile::exists(args.at(1))) {
            gameFileName = args.at(1);
        } else if (QFile::exists(args.at(1) + QString::fromLatin1(".hex"))) {
            gameFileName = args.at(1) + QString::fromLatin1(".hex");
        } else {
            qWarning() << "File" << args.at(1) << "not found.";
        }
    }

    int ret = 0;
    QMetaObject::invokeMethod(app, "entryPoint", Qt::QueuedConnection, Q_ARG(QString, gameFileName));
    ret = app->exec();
    delete app;
#ifndef DISABLE_VIDEO
    closeVideoEngine();
#endif
    closeSoundEngine();
    return ret;
}
