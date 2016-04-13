#include "qt/element/QtCodeFile.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "utility/ResourcePaths.h"
#include "utility/file/FileSystem.h"
#include "utility/logging/logging.h"
#include "utility/messaging/type/MessageActivateFile.h"
#include "utility/messaging/type/MessageChangeFileView.h"

#include "data/location/TokenLocation.h"
#include "data/location/TokenLocationFile.h"
#include "isTrial.h"
#include "qt/element/QtCodeFileList.h"
#include "qt/element/QtCodeSnippet.h"
#include "qt/utility/utilityQt.h"
#include "settings/ColorScheme.h"

QtCodeFile::QtCodeFile(const FilePath& filePath, QtCodeFileList* parent)
	: QFrame(parent)
	, m_updateTitleBarFunctor(std::bind(&QtCodeFile::doUpdateTitleBar, this))
	, m_parent(parent)
	, m_filePath(filePath)
	, m_snippetsRequested(false)
{
	setObjectName("code_file");

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setMargin(0);
	layout->setSpacing(0);
	layout->setAlignment(Qt::AlignTop);
	setLayout(layout);

	m_titleBar = new QPushButton(this);
	m_titleBar->setObjectName("title_widget");
	layout->addWidget(m_titleBar);

	QHBoxLayout* titleLayout = new QHBoxLayout();
	titleLayout->setMargin(0);
	titleLayout->setSpacing(0);
	titleLayout->setAlignment(Qt::AlignLeft);
	m_titleBar->setLayout(titleLayout);

	m_title = new QPushButton(filePath.fileName().c_str(), this);
	m_title->setObjectName("title_label");
	m_title->minimumSizeHint(); // force font loading
	m_title->setAttribute(Qt::WA_LayoutUsesWidgetRect); // fixes layouting on Mac
	m_title->setToolTip(QString::fromStdString(filePath.str()));
	m_title->setFixedWidth(m_title->fontMetrics().width(filePath.fileName().c_str()) + 52);
	m_title->setFixedHeight(std::max(m_title->fontMetrics().height() * 1.2, 28.0));
	m_title->setSizePolicy(sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);

	std::string text = ResourcePaths::getGuiPath() + "graph_view/images/file.png";

	m_title->setIcon(utility::colorizePixmap(
		QPixmap(text.c_str()),
		ColorScheme::getInstance()->getColor("code/file/title/icon").c_str()
	));

	titleLayout->addWidget(m_title);

	m_titleBar->setMinimumHeight(m_title->height() + 4);

	m_referenceCount = new QLabel(this);
	m_referenceCount->setObjectName("references_label");
	m_referenceCount->hide();
	titleLayout->addWidget(m_referenceCount);

	titleLayout->addStretch(3);

	m_minimizeButton = new QPushButton(this);
	m_minimizeButton->setObjectName("minimize_button");
	m_minimizeButton->setToolTip("minimize");
	m_minimizeButton->setAttribute(Qt::WA_LayoutUsesWidgetRect); // fixes layouting on Mac
	titleLayout->addWidget(m_minimizeButton);

	m_snippetButton = new QPushButton(this);
	m_snippetButton->setObjectName("snippet_button");
	m_snippetButton->setToolTip("show snippets");
	m_snippetButton->setAttribute(Qt::WA_LayoutUsesWidgetRect); // fixes layouting on Mac
	titleLayout->addWidget(m_snippetButton);

	m_maximizeButton = new QPushButton(this);
	m_maximizeButton->setObjectName("maximize_button");
	m_maximizeButton->setToolTip("maximize");
	m_maximizeButton->setAttribute(Qt::WA_LayoutUsesWidgetRect); // fixes layouting on Mac
	titleLayout->addWidget(m_maximizeButton);

	m_minimizeButton->setEnabled(false);
	m_snippetButton->setEnabled(false);
	m_maximizeButton->setEnabled(false);

	connect(m_titleBar, SIGNAL(clicked()), this, SLOT(clickedTitleBar()));
	connect(m_title, SIGNAL(clicked()), this, SLOT(clickedTitle()));
	connect(m_minimizeButton, SIGNAL(clicked()), this, SLOT(clickedMinimizeButton()));
	connect(m_snippetButton, SIGNAL(clicked()), this, SLOT(clickedSnippetButton()));
	connect(m_maximizeButton, SIGNAL(clicked()), this, SLOT(clickedMaximizeButton()));

	m_snippetLayout = new QVBoxLayout();
	layout->addLayout(m_snippetLayout);

	update();
}

QtCodeFile::~QtCodeFile()
{
}

void QtCodeFile::setModificationTime(TimePoint modificationTime)
{
	m_modificationTime = modificationTime;
	updateTitleBar();
}

const FilePath& QtCodeFile::getFilePath() const
{
	return m_filePath;
}

std::string QtCodeFile::getFileName() const
{
	return m_filePath.fileName();
}

const std::vector<Id>& QtCodeFile::getActiveTokenIds() const
{
	return m_parent->getActiveTokenIds();
}

const std::vector<Id>& QtCodeFile::getActiveLocalSymbolIds() const
{
	return m_parent->getActiveLocalSymbolIds();
}

const std::vector<Id>& QtCodeFile::getFocusedTokenIds() const
{
	return m_parent->getFocusedTokenIds();
}

std::vector<std::string> QtCodeFile::getErrorMessages() const
{
	return m_parent->getErrorMessages();
}

bool QtCodeFile::hasErrors() const
{
	return m_parent->hasErrors();;
}

void QtCodeFile::addCodeSnippet(const CodeSnippetParams& params)
{
	m_locationFile.reset();

	std::shared_ptr<QtCodeSnippet> snippet(new QtCodeSnippet(params, this));

	if (params.reduced)
	{
		m_title->hide();
	}

	m_snippetLayout->addWidget(snippet.get());

	if (params.locationFile->isWholeCopy)
	{
		snippet->setProperty("isFirst", true);
		snippet->setProperty("isLast", true);

		m_fileSnippet = snippet;
		if (!m_snippets.size())
		{
			m_fileSnippet->setIsActiveFile(true);
		}

		setMaximized();
		if (params.refCount != -1)
		{
			updateRefCount(0);
		}
		return;
	}

	m_snippets.push_back(snippet);

	setSnippets();
	updateRefCount(params.refCount);
}

QtCodeSnippet* QtCodeFile::insertCodeSnippet(const CodeSnippetParams& params)
{
	m_locationFile.reset();

	std::shared_ptr<QtCodeSnippet> snippet(new QtCodeSnippet(params, this));

	size_t i = 0;
	while (i < m_snippets.size())
	{
		uint start = snippet->getStartLineNumber();
		uint end = snippet->getEndLineNumber();

		std::shared_ptr<QtCodeSnippet> s = m_snippets[i];

		if (s->getEndLineNumber() + 1 < start)
		{
			i++;
			continue;
		}
		else if (s->getStartLineNumber() > end + 1)
		{
			break;
		}
		else if (s->getStartLineNumber() < start || s->getEndLineNumber() > end)
		{
			snippet = QtCodeSnippet::merged(snippet.get(), s.get(), this);
		}

		s->hide();
		m_snippetLayout->removeWidget(s.get());

		m_snippets.erase(m_snippets.begin() + i);
	}

	m_snippetLayout->insertWidget(i, snippet.get());
	m_snippets.insert(m_snippets.begin() + i, snippet);

	setSnippets();
	updateRefCount(params.refCount);

	return snippet.get();
}

QtCodeSnippet* QtCodeFile::findFirstActiveSnippet() const
{
	if (m_locationFile)
	{
		return nullptr;
	}

	if (m_maximizeButton->isEnabled())
	{
		for (std::shared_ptr<QtCodeSnippet> snippet : m_snippets)
		{
			if (snippet->isActive())
			{
				return snippet.get();
			}
		}
	}
	else
	{
		if (m_fileSnippet->isActive())
		{
			return m_fileSnippet.get();
		}
	}

	return nullptr;
}

bool QtCodeFile::isCollapsedActiveFile() const
{
	bool isActiveFile = false;
	if (m_locationFile)
	{
		std::vector<Id> ids = getActiveTokenIds();

		m_locationFile->forEachTokenLocation(
			[&](TokenLocation* location)
			{
				if (std::find(ids.begin(), ids.end(), location->getTokenId()) != ids.end())
				{
					isActiveFile = true;
				}
			}
		);
	}

	return isActiveFile;
}

void QtCodeFile::updateContent()
{
	updateSnippets();

	for (std::shared_ptr<QtCodeSnippet> snippet : m_snippets)
	{
		snippet->updateContent();
	}

	if (m_fileSnippet)
	{
		m_fileSnippet->updateContent();
	}
}

void QtCodeFile::setLocationFile(std::shared_ptr<TokenLocationFile> locationFile, int refCount)
{
	m_locationFile = locationFile;
	setMinimized();

	updateRefCount(refCount);
}

void QtCodeFile::setMinimized()
{
	for (std::shared_ptr<QtCodeSnippet> snippet : m_snippets)
	{
		snippet->hide();
	}

	if (m_fileSnippet)
	{
		m_fileSnippet->hide();
	}

	m_minimizeButton->setEnabled(false);
	if (m_snippets.size() || m_locationFile)
	{
		m_snippetButton->setEnabled(true);
	}
	m_maximizeButton->setEnabled(true);
}

void QtCodeFile::setSnippets()
{
	for (std::shared_ptr<QtCodeSnippet> snippet : m_snippets)
	{
		snippet->show();
	}

	if (m_fileSnippet)
	{
		m_fileSnippet->hide();
	}

	m_minimizeButton->setEnabled(true);
	m_snippetButton->setEnabled(false);
	m_maximizeButton->setEnabled(true);
}

void QtCodeFile::setMaximized()
{
	for (std::shared_ptr<QtCodeSnippet> snippet : m_snippets)
	{
		snippet->hide();
	}

	if (m_fileSnippet)
	{
		m_fileSnippet->show();
	}

	m_minimizeButton->setEnabled(true);
	if (m_snippets.size())
	{
		m_snippetButton->setEnabled(true);
	}
	m_maximizeButton->setEnabled(false);
}

void QtCodeFile::clickedTitleBar()
{
	if (m_minimizeButton->isEnabled())
	{
		clickedMinimizeButton();
	}
	else if (m_snippetButton->isEnabled())
	{
		clickedSnippetButton();
	}
	else
	{
		clickedMaximizeButton();
	}
}

void QtCodeFile::clickedTitle()
{
	MessageActivateFile(m_filePath).dispatch();
}

void QtCodeFile::clickedMinimizeButton() const
{
	MessageChangeFileView(
		m_filePath,
		MessageChangeFileView::FILE_MINIMIZED,
		false,
		hasErrors(),
		nullptr
	).dispatch();
}

void QtCodeFile::clickedSnippetButton() const
{
	MessageChangeFileView(
		m_filePath,
		MessageChangeFileView::FILE_SNIPPETS,
		(m_locationFile != nullptr),
		hasErrors(),
		m_locationFile
	).dispatch();
}

void QtCodeFile::clickedMaximizeButton() const
{
	MessageChangeFileView(
		m_filePath,
		MessageChangeFileView::FILE_MAXIMIZED,
		(m_fileSnippet == nullptr),
		hasErrors(),
		nullptr
	).dispatch();
}

void QtCodeFile::requestSnippets() const
{
	if (m_snippetsRequested)
	{
		return;
	}

	m_snippetsRequested = true;

	MessageChangeFileView msg(
		m_filePath,
		MessageChangeFileView::FILE_SNIPPETS,
		(m_locationFile != nullptr),
		hasErrors(),
		m_locationFile
	);
	msg.undoRedoType = MessageBase::UNDOTYPE_IGNORE;
	msg.dispatch();
}

bool QtCodeFile::hasSnippets() const
{
	return m_snippets.size() > 0;
}

void QtCodeFile::updateSnippets()
{
	if (m_snippets.size() == 0)
	{
		return;
	}

	int maxDigits = 1;
	for (std::shared_ptr<QtCodeSnippet> snippet : m_snippets)
	{
		snippet->setProperty("isFirst", false);
		snippet->setProperty("isLast", false);

		maxDigits = qMax(maxDigits, snippet->lineNumberDigits());
	}

	for (std::shared_ptr<QtCodeSnippet> snippet : m_snippets)
	{
		snippet->updateLineNumberAreaWidthForDigits(maxDigits);
	}

	m_snippets.front()->setProperty("isFirst", true);
	m_snippets.back()->setProperty("isLast", true);
}

void QtCodeFile::handleMessage(MessageWindowFocus* message)
{
	updateTitleBar();
}

void QtCodeFile::updateRefCount(int refCount)
{
	if (refCount > 0)
	{
		QString label = hasErrors() ? "error" : "reference";
		if (refCount > 1)
		{
			label += "s";
		}

		size_t fatalErrorCount = m_parent->getFatalErrorCountForFile(m_filePath);
		if (fatalErrorCount > 0)
		{
			label += " (" + QString::number(fatalErrorCount) + " fatal)";
		}

		m_referenceCount->setText(QString::number(refCount) + " " + label);
		m_referenceCount->show();
	}
	else
	{
		m_referenceCount->hide();
	}
}

void QtCodeFile::updateTitleBar()
{
	m_updateTitleBarFunctor();
}

void QtCodeFile::doUpdateTitleBar()
{
	if (isTrial())
	{
		return;
	}

	// cannot use m_filePath.exists() here since it is only checked when FilePath is constructed.
	if ((!FileSystem::exists(m_filePath.str())) ||
		(FileSystem::getLastWriteTime(m_filePath) > m_modificationTime))
	{
		std::string url = ResourcePaths::getGuiPath() + "code_view/images/pattern.png";
		std::string foo = "background-image: url(" + url + ");";
		m_title->setStyleSheet(foo.c_str());
	}
	else
	{
		m_title->setStyleSheet("");
	}
}
