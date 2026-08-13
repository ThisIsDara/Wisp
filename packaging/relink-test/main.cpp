// relink-test - LGPL compliance evidence (D-15).
//
// Proves Qt is DYNAMICALLY linked: this binary is compiled by
// verify-lgpl.ps1 against Qt's import library (Qt6Core.lib - a dynamic
// import, not a static lib) and then RUN with PATH set to the deploy
// folder ONLY. The Qt6Core.dll that gets loaded at runtime is therefore
// the DEPLOYED DLL (build/deploy/wisp), never the dev Qt at C:\Qt
// (RESEARCH Pitfall 5). A successful run shows an independent binary can
// link against Qt's public API and run against the shipped Qt DLLs -
// the LGPL "user may re-link" requirement.
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QString>
#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString libsPath = QLibraryInfo::path(QLibraryInfo::LibrariesPath);
    std::printf("RELINK OK - Qt6Core resolved from: %s\n", qPrintable(libsPath));
    return 0;
}
