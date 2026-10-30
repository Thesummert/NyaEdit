#include "BackgroundSettingsDialog.h"
#include "Editor.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace nya {
namespace {
class BackgroundPreview : public QWidget {
public:
    BackgroundPreview(EditorBackground *background, bool dark) : m_background(background), m_dark(dark)
    {
        setObjectName("backgroundWindowPreview");
        setAccessibleName(QStringLiteral("全窗口背景效果预览"));
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
        auto *toolbar = new QLabel(QStringLiteral("  NyaEdit    ·    文件    编辑    视图    设置"));
        toolbar->setContentsMargins(0, 6, 0, 6); layout->addWidget(toolbar);
        auto *row = new QHBoxLayout; row->setSpacing(0);
        auto *sidebar = new QLabel(QStringLiteral("  已打开的文档\n\n  welcome.cpp\n\n  notes.md"));
        sidebar->setAlignment(Qt::AlignTop); sidebar->setMinimumWidth(125); row->addWidget(sidebar);
        auto *editor = new Editor; editor->setObjectName("backgroundPreview");
        editor->setStyleSheet("QPlainTextEdit { background: transparent; }");
        editor->setAccessibleName(QStringLiteral("背景效果预览")); editor->setReadOnly(true);
        editor->setDark(dark); editor->setBackground(background, this); editor->setFontSize(11);
        editor->setPlainText(QStringLiteral("// NyaEdit · 全局背景预览\n"
            "轻盈落笔，专注文本。\n"
            "int main() {\n    return 0;\n}"));
        editor->setLanguage("cpp"); row->addWidget(editor, 1); layout->addLayout(row, 1);
        auto *status = new QLabel(QStringLiteral("  就绪                                       UTF-8   ·   LF"));
        status->setContentsMargins(0, 4, 0, 4); layout->addWidget(status);
        connect(background, &EditorBackground::changed, this, [this] { update(); });
    }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this); painter.fillRect(event->rect(), QColor(m_dark ? "#141820" : "#f5f7fa"));
        m_background->paint(painter, rect());
    }
private:
    EditorBackground *m_background;
    bool m_dark;
};
}

BackgroundSettingsDialog::BackgroundSettingsDialog(const BackgroundSettings &settings, bool dark, QWidget *parent) : QDialog(parent)
{
    setObjectName("backgroundSettingsDialog"); setWindowTitle(QStringLiteral("全局背景"));
    resize(680, 580); setMinimumSize(500, 500);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(24, 24, 24, 20); layout->setSpacing(14);
    auto *description = new QLabel(QStringLiteral("背景覆盖整个主窗口，包括欢迎页、侧栏、工具栏、标签栏和编辑区。图片等比填充窗口，超出部分居中裁剪。"));
    description->setWordWrap(true); layout->addWidget(description);
    auto *form = new QFormLayout; form->setSpacing(12);
    auto *pathRow = new QWidget; auto *pathLayout = new QHBoxLayout(pathRow); pathLayout->setContentsMargins(0, 0, 0, 0);
    m_path = new QLineEdit(settings.path); m_path->setObjectName("backgroundPath");
    m_path->setAccessibleName(QStringLiteral("背景图片路径")); m_path->setPlaceholderText(QStringLiteral("未设置背景"));
    m_path->setClearButtonEnabled(true);
    auto *browse = new QPushButton(QStringLiteral("选择图片…"));
    pathLayout->addWidget(m_path, 1); pathLayout->addWidget(browse); form->addRow(QStringLiteral("背景图片(&I)"), pathRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        QStringList patterns;
        for (const auto &format : QImageReader::supportedImageFormats()) patterns << "*." + QString::fromLatin1(format);
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择背景图片"), m_path->text(),
            QStringLiteral("图片 (%1);;所有文件 (*)").arg(patterns.join(' ')));
        if (!path.isEmpty()) m_path->setText(path);
    });
    const auto addControl = [form](const QString &label, const char *name, int maximum, int value) {
        auto *row = new QWidget; auto *line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal); slider->setRange(0, maximum); slider->setAccessibleName(label);
        slider->setObjectName(QString::fromLatin1(name) + "Slider");
        auto *spin = new QSpinBox; spin->setObjectName(name); spin->setRange(0, maximum); spin->setAccessibleName(label);
        spin->setValue(value); slider->setValue(spin->value());
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
        line->addWidget(slider, 1); line->addWidget(spin); form->addRow(label, row);
        return spin;
    };
    m_opacity = addControl(QStringLiteral("背景不透明度(&O)"), "backgroundOpacity", 100, settings.opacity); m_opacity->setSuffix(" %");
    m_blur = addControl(QStringLiteral("高斯模糊强度(&B)"), "backgroundBlur", 40, settings.blur);
    layout->addLayout(form);
    auto *hint = new QLabel(QStringLiteral("不透明度 0% 为完全透明，100% 为完全不透明；模糊 0 为原图，数值越大越模糊。图片丢失或无法读取时自动取消背景。"));
    hint->setWordWrap(true); layout->addWidget(hint);
    m_background = new EditorBackground(this);
    layout->addWidget(new BackgroundPreview(m_background, dark), 1);
    m_status = new QLabel; m_status->setObjectName("backgroundStatus"); m_status->setWordWrap(true); layout->addWidget(m_status);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); m_buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    m_buttons->button(QDialogButtonBox::Reset)->setText(QStringLiteral("恢复默认"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &BackgroundSettingsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        m_path->clear(); m_opacity->setValue(20); m_blur->setValue(0); updatePreview();
    });
    layout->addWidget(m_buttons);
    m_previewTimer.setSingleShot(true); m_previewTimer.setInterval(80);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] { updatePreview(); });
    connect(m_path, &QLineEdit::textChanged, this, [this] { m_previewTimer.start(); });
    connect(m_opacity, qOverload<int>(&QSpinBox::valueChanged), this, [this] { m_previewTimer.start(); });
    connect(m_blur, qOverload<int>(&QSpinBox::valueChanged), this, [this] { m_previewTimer.start(); });
    connect(m_background, &EditorBackground::fileUnavailable, this, [this] {
        m_status->setText(QStringLiteral("图片不存在或无法读取，请重新选择图片，或清空路径以取消背景。"));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    });
    updatePreview();
}

BackgroundSettings BackgroundSettingsDialog::selectedSettings() const
{
    return {m_path->text(), m_opacity->value(), m_blur->value()};
}

bool BackgroundSettingsDialog::updatePreview()
{
    m_previewTimer.stop();
    const bool valid = m_background->setSettings(selectedSettings());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    m_status->setText(valid ? (m_path->text().isEmpty() ? QStringLiteral("当前使用主题底色。") : QStringLiteral("实时预览 · 确定后保存，取消不修改设置。"))
                            : QStringLiteral("图片不存在或无法读取，请重新选择图片，或清空路径以取消背景。"));
    return valid;
}

void BackgroundSettingsDialog::accept()
{
    if (updatePreview()) QDialog::accept();
}
}
