#include "TaskPanel.h"
#include "Editor.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace nya {
namespace { constexpr int MaximumOutputCharacters = 1024 * 1024; }

TaskPanel::TaskPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("taskPanel");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(12, 6, 12, 8); layout->setSpacing(4);
    auto *header = new QHBoxLayout;
    m_title = new QLabel(QStringLiteral("尚未运行任务")); m_title->setObjectName("taskTitle"); m_title->setTextFormat(Qt::PlainText);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse); m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(m_title, 1);
    auto *clear = new QToolButton; clear->setText(QStringLiteral("清空输出")); clear->setObjectName("taskClearOutput");
    connect(clear, &QToolButton::clicked, this, [this] { m_pending.clear(); m_output->clear(); });
    header->addWidget(clear);
    m_stop = new QToolButton; m_stop->setText(QStringLiteral("停止")); m_stop->setObjectName("taskStop"); m_stop->setEnabled(false);
    connect(m_stop, &QToolButton::clicked, this, &TaskPanel::stopRequested); header->addWidget(m_stop); layout->addLayout(header);
    m_output = new QPlainTextEdit; m_output->setObjectName("taskOutput"); m_output->setReadOnly(true);
    m_output->setAccessibleName(QStringLiteral("任务命令输出")); m_output->setMaximumBlockCount(10000);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap); layout->addWidget(m_output, 1); refreshFont();
    m_flushTimer.setSingleShot(true); m_flushTimer.setInterval(30);
    connect(&m_flushTimer, &QTimer::timeout, this, &TaskPanel::flush);
}

void TaskPanel::refreshFont() { m_output->setFont(Editor::defaultEditorFont()); }

void TaskPanel::beginTask(const QString &label, const QString &workspaceFolder)
{
    m_pending.clear(); m_output->clear(); m_escapeState = EscapeState::Normal; m_afterCarriageReturn = false;
    m_title->setText(label + QStringLiteral("  ·  ") + workspaceFolder); m_title->setToolTip(m_title->text());
}

QString TaskPanel::plainOutput(const QString &text)
{
    QString result; result.reserve(text.size());
    for (const QChar c : text) {
        switch (m_escapeState) {
        case EscapeState::Normal:
            if (c.unicode() == 0x1b) m_escapeState = EscapeState::Escape;
            else if (c == '\r') { result += '\n'; m_afterCarriageReturn = true; }
            else if (c == '\n') { if (!m_afterCarriageReturn) result += c; m_afterCarriageReturn = false; }
            else { m_afterCarriageReturn = false; if (c == '\t' || c.unicode() >= 0x20) result += c; }
            break;
        case EscapeState::Escape:
            m_escapeState = c == '[' ? EscapeState::Csi : c == ']' ? EscapeState::Osc : EscapeState::Normal; break;
        case EscapeState::Csi:
            if (c.unicode() >= 0x40 && c.unicode() <= 0x7e) m_escapeState = EscapeState::Normal;
            break;
        case EscapeState::Osc:
            if (c.unicode() == 7) m_escapeState = EscapeState::Normal;
            else if (c.unicode() == 0x1b) m_escapeState = EscapeState::OscEscape;
            break;
        case EscapeState::OscEscape:
            m_escapeState = c == '\\' ? EscapeState::Normal : EscapeState::Osc; break;
        }
    }
    return result;
}

void TaskPanel::appendOutput(const QString &text)
{
    m_pending += plainOutput(text);
    if (m_pending.size() > MaximumOutputCharacters) m_pending = m_pending.right(MaximumOutputCharacters);
    if (!m_pending.isEmpty() && m_pending.at(0).isLowSurrogate()) m_pending.remove(0, 1);
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}

void TaskPanel::flush()
{
    m_flushTimer.stop();
    if (m_pending.isEmpty()) return;
    auto *scroll = m_output->verticalScrollBar(); const bool follow = scroll->value() >= scroll->maximum() - 2;
    auto cursor = QTextCursor(m_output->document()); cursor.movePosition(QTextCursor::End);
    cursor.insertText(m_pending); m_pending.clear();
    int excess = m_output->document()->characterCount() - 1 - MaximumOutputCharacters;
    if (excess > 0) {
        if (m_output->document()->characterAt(excess).isLowSurrogate()) ++excess;
        cursor.movePosition(QTextCursor::Start); cursor.setPosition(excess, QTextCursor::KeepAnchor); cursor.removeSelectedText();
    }
    if (follow) scroll->setValue(scroll->maximum());
}

void TaskPanel::setRunning(bool running)
{
    m_stop->setEnabled(running);
    if (!running) flush();
}
}
