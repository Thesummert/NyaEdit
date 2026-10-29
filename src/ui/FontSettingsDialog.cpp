#include "FontSettingsDialog.h"
#include "Editor.h"
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace nya {
FontSettingsDialog::FontSettingsDialog(const QFont &font, QWidget *parent) : QDialog(parent)
{
    setObjectName("fontSettingsDialog"); setWindowTitle(QStringLiteral("字体与字号"));
    resize(560, 390); setMinimumSize(400, 320);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(24, 24, 24, 20); layout->setSpacing(16);
    auto *description = new QLabel(QStringLiteral("选择编辑区字体，确认后应用于所有文档。"));
    description->setWordWrap(true); layout->addWidget(description);
    auto *form = new QFormLayout; form->setSpacing(12);
    m_family = new QFontComboBox; m_family->setObjectName("editorFontFamily");
    m_family->setCurrentFont(font); m_family->setAccessibleName(QStringLiteral("编辑器字体"));
    form->addRow(QStringLiteral("字体(&F)"), m_family);
    m_size = new QSpinBox; m_size->setObjectName("editorFontSize");
    m_size->setRange(Editor::MinimumFontSize, Editor::MaximumFontSize); m_size->setSuffix(" pt");
    m_size->setValue(font.pointSize()); m_size->setAccessibleName(QStringLiteral("编辑器字号"));
    form->addRow(QStringLiteral("字号(&S)"), m_size); layout->addLayout(form);
    m_preview = new QPlainTextEdit; m_preview->setObjectName("fontPreview"); m_preview->setReadOnly(true);
    m_preview->setAccessibleName(QStringLiteral("字体预览"));
    m_preview->setPlainText(QStringLiteral("NyaEdit · 轻盈落笔，专注文本。\n"
        "The quick brown fox jumps over the lazy dog.\n"
        "0123456789   () [] {} <>   += / *\n"
        "int main() { return 0; }"));
    layout->addWidget(m_preview, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    buttons->setObjectName("fontSettingsButtons");
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Reset)->setText(QStringLiteral("恢复默认"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        const auto font = Editor::defaultEditorFont(); m_family->setCurrentFont(font); m_size->setValue(font.pointSize());
    });
    layout->addWidget(buttons);
    connect(m_family, &QFontComboBox::currentFontChanged, this, [this] { updatePreview(); });
    connect(m_size, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updatePreview(); });
    updatePreview();
}

QFont FontSettingsDialog::selectedFont() const
{
    auto font = m_family->currentFont(); font.setPointSize(m_size->value()); return font;
}
void FontSettingsDialog::updatePreview() { m_preview->setFont(selectedFont()); }
}
