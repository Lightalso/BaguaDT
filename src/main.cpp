#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QFile>
#include <QDockWidget>
#include <QFont>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char **argv) {
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24);
    f.setStencilBufferSize(8);
    f.setSamples(4);
    QSurfaceFormat::setDefaultFormat(f);
    QApplication app(argc, argv);
    app.setApplicationName("BaguaDT");
    app.setApplicationVersion("0.3.0");
    app.setOrganizationName("BaguaDT");
    bagua::applyEditorTheme(app);
    bagua::MainWindow w;
    w.show();
    const auto args = app.arguments();
    for(const QString flag:{"--hsc","--record","--capture","--simulate-steps","--replay-time"}){
        const int index=args.indexOf(flag);if(index>=0 && (index+1>=args.size() || args[index+1].startsWith("--")))return 2;
    }
    if(args.contains("--record") && !args.contains("--simulate-steps"))return 2;
    w.setProperty("nonInteractive",args.contains("--record") || args.contains("--simulate-steps") || args.contains("--capture"));
    if (args.size() > 1 && !args[1].startsWith("--"))
        if(!w.openPath(args[1]))return 2;
    if(args.contains("--show-assembly"))if(auto *dock=w.findChild<QDockWidget *>("assemblyDock"))dock->raise();
    const int replayTime = args.indexOf("--replay-time");
    if (replayTime >= 0 && replayTime + 1 < args.size()) {
        bool ok;
        double t = args[replayTime + 1].toDouble(&ok);
        if(!ok)return 2;
        if(!w.seekReplay(t))return 3;
    }
    const int hsc = args.indexOf("--hsc");
    if (hsc >= 0 && hsc + 1 < args.size()) {
        auto options = w.physicsOptions();
        if(args[hsc+1]!="on" && args[hsc+1]!="off")return 2;
        options.enabled = args[hsc + 1] == "on";
        w.setPhysicsOptions(options);
    }
    // Batch capture: start the runtime, record one frame per fixed step, then write the file.
    const int record = args.indexOf("--record");
    const bool batch = record >= 0 && record + 1 < args.size();
    if (batch) {
        if(!w.startSimulation())return 3;
        // Batch mode records exactly one frame per fixed step, i.e. lock-step sampling.
        w.setOfflineCapture(true);
        if(!w.beginRecording())return 3;
    }
    // Free-running playback for QA/demo: starts the timer-driven loop instead of stepping.
    const int play = args.indexOf("--play");
    if (play >= 0 && !batch)
        if(!w.startSimulation())return 3;
    const int steps = args.indexOf("--simulate-steps");
    if (steps >= 0 && steps + 1 < args.size()) {
        bool ok;
        int count = args[steps + 1].toInt(&ok);
        if(!ok || count<=0 || count>200000)return 2;
        for(int i=0;i<count;++i)if(!w.stepSimulation())return 3;
    }
    const int capture = args.indexOf("--capture");
    if (batch) {
        if(steps<0)return 2;
        if(!w.saveRecording(args[record + 1]))return 4;
        // A batch export is done once the recording is written; keep the window open only
        // when a deterministic screenshot was requested as well.
        if (capture < 0)
            QTimer::singleShot(0, &w, &QWidget::close);
    }
    // Deterministic visual capture for QA without controlling the user's desktop.
    if (capture >= 0 && capture + 1 < args.size())
        QTimer::singleShot(1800, &w, [&w, args, capture] {
            const bool saved=w.grab().save(args[capture + 1]);
            w.scene->undo.setClean();
            w.close();
            if(!saved)QApplication::exit(4);
        });
    return app.exec();
}
