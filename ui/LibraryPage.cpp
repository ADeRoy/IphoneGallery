#include "LibraryPage.h"

#include "ElaComboBox.h"
#include "ElaDef.h"
#include "ElaIcon.h"
#include "ElaProgressRing.h"
#include "ElaPushButton.h"
#include "ElaSlider.h"
#include "ElaText.h"
#include "ElaTheme.h"
#include "MediaUtils.h"
#include "PhotoGridView.h"
#include "PhotoLibrary.h"
#include "ThumbnailService.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
QColor themeColor(ElaThemeType::ThemeColor color)
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), color);
}

QLabel* mutedLabel(const QString& text, int pixelSize, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPixelSize(pixelSize);
    label->setFont(font);
    label->setStyleSheet(QStringLiteral("color:#8A8A93; background:transparent;"));
    return label;
}

QString countText(qint64 value)
{
    return QLocale().toString(value);
}
} // namespace

LibraryPage::LibraryPage(Mode mode, ThumbnailService* thumbnails, QWidget* parent)
    : QWidget(parent)
    , _mode(mode)
    , _thumbnails(thumbnails)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setFixedHeight(92);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(28, 18, 28, 10);
    headerLayout->setSpacing(16);

    auto* titleLayout = new QVBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);

    _titleLabel = new ElaText(header);
    _titleLabel->setTextPixelSize(27);
    QFont titleFont = _titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    _titleLabel->setFont(titleFont);

    _summaryLabel = mutedLabel(QString(), 12, header);
    _summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    titleLayout->addWidget(_titleLabel);
    titleLayout->addWidget(_summaryLabel);
    titleLayout->addStretch();

    auto* controlsLayout = new QHBoxLayout;
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(10);
    controlsLayout->setAlignment(Qt::AlignVCenter);

    _monthCombo = new ElaComboBox(header);
    _monthCombo->setFixedWidth(138);
    _monthCombo->setToolTip(QStringLiteral("跳转到月份"));

    _sortCombo = new ElaComboBox(header);
    _sortCombo->addItem(QStringLiteral("拍摄时间 · 新→旧"));
    _sortCombo->addItem(QStringLiteral("拍摄时间 · 旧→新"));
    _sortCombo->addItem(QStringLiteral("文件名"));
    _sortCombo->setFixedWidth(146);

    _sizeSlider = new ElaSlider(Qt::Horizontal, header);
    _sizeSlider->setRange(112, 260);
    _sizeSlider->setSingleStep(8);
    _sizeSlider->setPageStep(24);
    _sizeSlider->setValue(168);
    _sizeSlider->setFixedWidth(126);
    _sizeSlider->setToolTip(QStringLiteral("缩略图大小"));

    controlsLayout->addWidget(_monthCombo);
    controlsLayout->addWidget(_sortCombo);
    controlsLayout->addWidget(_sizeSlider);

    headerLayout->addLayout(titleLayout, 1);
    headerLayout->addLayout(controlsLayout);

    rootLayout->addWidget(header);

    _stack = new QStackedWidget(this);
    _stack->setStyleSheet(QStringLiteral("QStackedWidget{background:transparent;}"));
    rootLayout->addWidget(_stack, 1);

    _grid = new PhotoGridView(_stack);
    _grid->setThumbnailService(_thumbnails);
    _stack->addWidget(_grid);

    _emptyPage = new QWidget(_stack);
    auto* emptyLayout = new QVBoxLayout(_emptyPage);
    emptyLayout->setContentsMargins(40, 40, 40, 40);
    emptyLayout->setSpacing(10);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto* emptyIcon = new QLabel(_emptyPage);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(ElaIcon::getInstance()
                             ->getElaIcon(ElaIconType::Images, 64,
                                          themeColor(ElaThemeType::BasicTextDisable))
                             .pixmap(64, 64));
    emptyLayout->addWidget(emptyIcon, 0, Qt::AlignHCenter);

    _emptyTitle = new ElaText(_emptyPage);
    _emptyTitle->setTextPixelSize(21);
    QFont emptyFont = _emptyTitle->font();
    emptyFont.setWeight(QFont::DemiBold);
    _emptyTitle->setFont(emptyFont);
    _emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(_emptyTitle);

    _emptyHint = mutedLabel(QString(), 13, _emptyPage);
    _emptyHint->setAlignment(Qt::AlignCenter);
    _emptyHint->setWordWrap(true);
    emptyLayout->addWidget(_emptyHint);

    _emptyButton = new ElaPushButton(QStringLiteral("打开 iPhone 照片文件夹"), _emptyPage);
    _emptyButton->setFixedSize(218, 40);
    emptyLayout->addWidget(_emptyButton, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(8);
    connect(_emptyButton, &ElaPushButton::clicked, this, &LibraryPage::openFolderRequested);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this, emptyIcon]() {
        emptyIcon->setPixmap(ElaIcon::getInstance()
                                 ->getElaIcon(ElaIconType::Images, 64,
                                              themeColor(ElaThemeType::BasicTextDisable))
                                 .pixmap(64, 64));
        update();
    });

    _stack->addWidget(_emptyPage);

    _loadingPage = new QWidget(_stack);
    auto* loadingLayout = new QVBoxLayout(_loadingPage);
    loadingLayout->setContentsMargins(40, 40, 40, 40);
    loadingLayout->setSpacing(12);
    loadingLayout->setAlignment(Qt::AlignCenter);

    _progressRing = new ElaProgressRing(_loadingPage);
    _progressRing->setFixedSize(54, 54);
    _progressRing->setIsBusying(true);
    loadingLayout->addWidget(_progressRing, 0, Qt::AlignHCenter);

    _loadingTitle = new ElaText(QStringLiteral("正在读取照片"), _loadingPage);
    _loadingTitle->setTextPixelSize(19);
    _loadingTitle->setAlignment(Qt::AlignCenter);
    loadingLayout->addWidget(_loadingTitle);

    _loadingHint = mutedLabel(QString(), 12, _loadingPage);
    _loadingHint->setAlignment(Qt::AlignCenter);
    _loadingHint->setWordWrap(true);
    loadingLayout->addWidget(_loadingHint);

    _stack->addWidget(_loadingPage);

    connect(_grid, &PhotoGridView::itemActivated, this, &LibraryPage::itemActivated);
    connect(_grid, &PhotoGridView::deleteRequested, this, &LibraryPage::deleteRequested);
    connect(_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                _sortMode = index;
                refresh();
                _grid->verticalScrollBar()->setValue(0);
            });
    connect(_sizeSlider, &QSlider::valueChanged, this,
            [this](int value) { _grid->setThumbSize(value); });
    connect(_monthCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString monthKey = _monthCombo->itemData(index).toString();
        if (!monthKey.isEmpty())
        {
            _grid->scrollToMonth(monthKey);
            _monthCombo->setCurrentIndex(0);
        }
    });

    switch (_mode)
    {
    case Mode::All:
        _titleLabel->setText(QStringLiteral("图库"));
        _emptyTitle->setText(QStringLiteral("还没有照片"));
        _emptyHint->setText(QStringLiteral("选择一个 iPhone 照片文件夹，即可按时间浏览照片、"
                                          "实况照片与视频。"));
        break;
    case Mode::Live:
        _titleLabel->setText(QStringLiteral("实况照片"));
        _emptyTitle->setText(QStringLiteral("没有实况照片"));
        _emptyHint->setText(QStringLiteral("实况照片由照片和同名、时长约 2 秒的 MOV 组成；"
                                          "超过 3 秒的 MOV 会按视频处理。"));
        break;
    case Mode::Video:
        _titleLabel->setText(QStringLiteral("视频"));
        _emptyTitle->setText(QStringLiteral("没有视频"));
                _emptyHint->setText(QStringLiteral("视频页面显示超过 3 秒的视频；实况照片不在其中。"));
        break;
    }

    _summaryLabel->setText(QStringLiteral("正在准备…"));
    _stack->setCurrentWidget(_emptyPage);
}

LibraryPage::~LibraryPage() = default;

void LibraryPage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), themeColor(ElaThemeType::BasicBase));
}

void LibraryPage::setItems(const PhotoItemList& items)
{
    _sourceItems = items;
    refresh();
}

void LibraryPage::removeItem(const PhotoItemPtr& item)
{
    if (!item)
    {
        return;
    }
    auto matches = [&item](const PhotoItemPtr& candidate) {
        return candidate && (candidate.get() == item.get()
                             || (candidate->imagePath == item->imagePath
                                 && candidate->videoPath == item->videoPath));
    };
    for (int i = _sourceItems.size() - 1; i >= 0; --i)
    {
        if (matches(_sourceItems.at(i)))
        {
            _sourceItems.removeAt(i);
        }
    }
    for (int i = _visibleItems.size() - 1; i >= 0; --i)
    {
        if (matches(_visibleItems.at(i)))
        {
            _visibleItems.removeAt(i);
        }
    }
    _grid->removeItem(item);
    rebuildMonthCombo();
    updateSummary();
    updateEmptyState();
}

void LibraryPage::setSearchText(const QString& text)
{
    if (_searchText == text)
    {
        return;
    }
    _searchText = text;
    refresh();
    _grid->verticalScrollBar()->setValue(0);
}

void LibraryPage::setLoading(bool loading, const QString& folderPath)
{
    _isLoading = loading;
    if (loading)
    {
        _loadingHint->setText(folderPath);
        _stack->setCurrentWidget(_loadingPage);
        _summaryLabel->setText(QStringLiteral("正在扫描文件夹…"));
        _monthCombo->setEnabled(false);
        _sortCombo->setEnabled(false);
        _sizeSlider->setEnabled(false);
    }
    else
    {
        _monthCombo->setEnabled(true);
        _sortCombo->setEnabled(true);
        _sizeSlider->setEnabled(true);
        updateEmptyState();
        updateSummary();
    }
}

void LibraryPage::refresh()
{
    _visibleItems.clear();
    const QString needle = _searchText.trimmed();
    for (const PhotoItemPtr& item : _sourceItems)
    {
        if (!item)
        {
            continue;
        }
        switch (_mode)
        {
        case Mode::All:
            break;
        case Mode::Live:
            if (!item->isLivePhoto)
            {
                continue;
            }
            break;
        case Mode::Video:
            if (!item->hasVideo() || item->isLivePhoto)
            {
                continue;
            }
            break;
        }

        if (!needle.isEmpty()
            && !item->displayName().contains(needle, Qt::CaseInsensitive)
            && !item->title().contains(needle, Qt::CaseInsensitive))
        {
            continue;
        }
        _visibleItems.append(item);
    }

    applySort();
    _grid->setItems(_visibleItems);
    rebuildMonthCombo();
    updateSummary();

    if (!_isLoading)
    {
        updateEmptyState();
    }
}

void LibraryPage::applySort()
{
    const int sortMode = _sortMode;
    std::sort(_visibleItems.begin(), _visibleItems.end(),
              [sortMode](const PhotoItemPtr& left, const PhotoItemPtr& right) {
                  const int monthCompare = QString::compare(left->monthKey, right->monthKey);
                  if (monthCompare != 0)
                  {
                      return sortMode == 1 ? monthCompare < 0 : monthCompare > 0;
                  }
                  if (sortMode == 0)
                  {
                      const QDateTime leftTime = left->dateTime();
                      const QDateTime rightTime = right->dateTime();
                      if (leftTime != rightTime)
                      {
                          return leftTime > rightTime;
                      }
                  }
                  else if (sortMode == 1)
                  {
                      const QDateTime leftTime = left->dateTime();
                      const QDateTime rightTime = right->dateTime();
                      if (leftTime != rightTime)
                      {
                          return leftTime < rightTime;
                      }
                  }
                  return QString::localeAwareCompare(left->displayName(), right->displayName())
                         < 0;
              });
}

void LibraryPage::rebuildMonthCombo()
{
    const QString previous = _monthCombo->currentData().toString();
    QSignalBlocker blocker(_monthCombo);
    _monthCombo->clear();
    _monthCombo->addItem(QStringLiteral("跳转到月份"), QString());
    QSet<QString> seen;
    for (const PhotoItemPtr& item : _visibleItems)
    {
        if (!item || item->monthKey.isEmpty() || seen.contains(item->monthKey))
        {
            continue;
        }
        seen.insert(item->monthKey);
        _monthCombo->addItem(PhotoLibrary::monthTitle(item->monthKey), item->monthKey);
    }
    const int restored = _monthCombo->findData(previous);
    _monthCombo->setCurrentIndex(restored > 0 ? restored : 0);
    _monthCombo->setEnabled(_monthCombo->count() > 1);
}

void LibraryPage::updateSummary()
{
    qint64 photos = 0;
    qint64 videos = 0;
    qint64 live = 0;
    qint64 totalBytes = 0;
    for (const PhotoItemPtr& item : _visibleItems)
    {
        if (item->isLivePhoto)
        {
            ++live;
        }
        else if (item->hasVideo())
        {
            ++videos;
        }
        else if (item->hasImage())
        {
            ++photos;
        }
        totalBytes += item->imageBytes + item->videoBytes;
    }

    QString summary;
    switch (_mode)
    {
    case Mode::All:
        summary = QStringLiteral("%1 张照片 · %2 段视频 · %3 张实况")
                      .arg(countText(photos), countText(videos), countText(live));
        break;
    case Mode::Live:
        summary = QStringLiteral("%1 张实况照片").arg(countText(live));
        break;
    case Mode::Video:
        summary = QStringLiteral("%1 段视频").arg(countText(videos));
        break;
    }

    if (!_visibleItems.isEmpty())
    {
        summary += QStringLiteral(" · %1").arg(MediaUtils::formatBytes(totalBytes));
    }
    if (!_searchText.trimmed().isEmpty())
    {
        summary += QStringLiteral(" · 搜索“%1”").arg(_searchText.trimmed());
    }
    _summaryLabel->setText(summary);
}

void LibraryPage::updateEmptyState()
{
    if (_visibleItems.isEmpty())
    {
        if (_sourceItems.isEmpty())
        {
            _emptyTitle->setText(QStringLiteral("还没有照片"));
            _emptyHint->setText(QStringLiteral("选择一个 iPhone 照片文件夹，即可按时间浏览照片、"
                                              "实况照片与视频。"));
            _emptyButton->setVisible(true);
        }
        else
        {
            _emptyButton->setVisible(true);
            if (!_searchText.trimmed().isEmpty())
            {
                _emptyTitle->setText(QStringLiteral("没有找到匹配的照片"));
                _emptyHint->setText(QStringLiteral("试试其他关键词，或清空搜索。"));
                _emptyButton->setVisible(false);
            }
            else if (_mode == Mode::Live)
            {
                _emptyTitle->setText(QStringLiteral("没有实况照片"));
                _emptyHint->setText(QStringLiteral("实况照片由照片和同名、时长约 2 秒的 MOV 组成；"
                                                  "超过 3 秒的 MOV 会按视频处理。"));
            }
            else if (_mode == Mode::Video)
            {
                _emptyTitle->setText(QStringLiteral("没有视频"));
        _emptyHint->setText(QStringLiteral("视频页面显示超过 3 秒的视频；实况照片不在其中。"));
            }
        }
        _stack->setCurrentWidget(_emptyPage);
    }
    else
    {
        _stack->setCurrentWidget(_grid);
    }
}
