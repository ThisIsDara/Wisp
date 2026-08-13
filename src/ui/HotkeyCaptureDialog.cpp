#include "ui/HotkeyCaptureDialog.h"

#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

namespace {

// Token-level parse, no regex (T-02-03-02): split the portable sequence on
// '+'; a token is a modifier when it case-insensitively equals Alt/Ctrl/
// Control/Shift/Meta; F12 is kernel-reserved and may not appear anywhere
// (RESEARCH.md §1).
bool tokenIsModifier(const QString &token)
{
    const QString t = token.toLower();
    return t == QStringLiteral("alt") || t == QStringLiteral("ctrl")
        || t == QStringLiteral("control") || t == QStringLiteral("shift")
        || t == QStringLiteral("meta");
}

} // namespace

HotkeyCaptureDialog::HotkeyCaptureDialog(QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
}

bool HotkeyCaptureDialog::validateSequence(const QString &portable)
{
    if (portable.isEmpty())
        return false;

    const QStringList tokens = portable.split(QLatin1Char('+'));
    if (tokens.contains(QStringLiteral("F12"), Qt::CaseInsensitive))
        return false; // kernel-reserved debugger combo (RESEARCH §1)

    // A single modifier token ("Alt"/"Ctrl"/"Shift"/"Meta") is not a combo.
    if (tokens.size() == 1 && tokenIsModifier(tokens.first()))
        return false;

    return true;
}

void HotkeyCaptureDialog::open(const QString &currentSequence)
{
    if (m_dialog) {
        // CR-02 (2026-08-12): the window survives cancelDialog's close()
        // (root-context parent — the QObject is never destroyed), so the old
        // `if (m_dialog) return;` gate made the dialog single-use per session:
        // the tray "Change hotkey…" and the settings hotkey row both went
        // dead after the first capture. Reuse the hidden instance instead:
        // refresh the seed text, re-show, refocus the capture field. The
        // QML side resets capturedSequence on hide (onVisibleChanged).
        m_dialog->setProperty("currentSequence", currentSequence);
        QMetaObject::invokeMethod(m_dialog, "show");
        m_dialog->requestActivate();
        if (QQuickItem *field = m_dialog->findChild<QQuickItem *>(QStringLiteral("keyField")))
            field->forceActiveFocus();
        return;
    }

    // 2026-08-12: load via the MODULE import system (matches
    // engine.loadFromModule) instead of a hardcoded qrc URL — the module's
    // resources live under :/qt/qml/wisp/qml/ (QML_FILES keep their qml/
    // subdir with QTP0001), so the old literal URL could never resolve:
    // the dialog silently failed to load and the tray/settings handoffs
    // appeared dead. The qmldir maps the type name to the real path.
    QQmlComponent component(m_engine);
    component.setData("import wisp\nHotkeyCaptureDialog {\n}",
                      QUrl(QStringLiteral("qrc:/qt/qml/wisp/HotkeyCaptureDialog.qml")));
    if (!component.isReady()) {
        for (const auto &e : component.errors())
            qWarning() << e.toString();
        emit cancelled();
        return;
    }

    // Host injection: QML cannot reach `this` otherwise (plan REQUIRED step) —
    // per-instance beginCreate/setProperty, no global context pollution.
    QObject *dialogObj = component.beginCreate(m_engine->rootContext());
    dialogObj->setProperty("dialogHost", QVariant::fromValue(this));
    dialogObj->setProperty("currentSequence", currentSequence);
    component.completeCreate();

    m_dialog = qobject_cast<QQuickWindow *>(dialogObj);
}

void HotkeyCaptureDialog::submitSequence(const QString &portable)
{
    if (!validateSequence(portable)) {
        // Invalid — the QML side shows the red rejection label; signal nothing.
        if (m_dialog)
            QMetaObject::invokeMethod(m_dialog, "showValidationError",
                                      Q_ARG(QString, portable));
        return;
    }

    emit accepted(portable);
    if (m_dialog)
        m_dialog->hide();
}