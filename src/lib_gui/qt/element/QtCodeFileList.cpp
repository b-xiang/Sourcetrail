#include "qt/element/QtCodeFileList.h"

#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

#include "utility/file/FileSystem.h"
#include "utility/messaging/type/MessageScrollCode.h"

#include "data/location/TokenLocationFile.h"
#include "qt/element/QtCodeFile.h"
#include "qt/element/QtCodeSnippet.h"

QtCodeFileList::QtCodeFileList(QWidget* parent)
	: QScrollArea(parent)
	, m_scrollToFile(nullptr)
	, m_value(0)
{
	setObjectName("code_file_list_base");

	m_frame = std::make_shared<QFrame>(this);
	m_frame->setObjectName("code_file_list");

	QVBoxLayout* layout = new QVBoxLayout(m_frame.get());
	layout->setSpacing(8);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setAlignment(Qt::AlignTop);
	m_frame->setLayout(layout);

	setWidgetResizable(true);
	setWidget(m_frame.get());

	connect(this->verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(scrolled(int)));
	connect(this, SIGNAL(shouldScrollToSnippet(QtCodeSnippet*, uint)), this, SLOT(scrollToSnippet(QtCodeSnippet*, uint)), Qt::QueuedConnection);
}

QtCodeFileList::~QtCodeFileList()
{
}

QSize QtCodeFileList::sizeHint() const
{
	return QSize(800, 800);
}

void QtCodeFileList::addCodeSnippet(
	const CodeSnippetParams& params,
	bool insert
){
	QtCodeFile* file = getFile(params.locationFile->getFilePath());

	if (insert)
	{
		QtCodeSnippet* snippet = file->insertCodeSnippet(params);
		emit shouldScrollToSnippet(snippet, params.startLineNumber);
	}
	else
	{
		file->addCodeSnippet(params);
	}

	file->setModificationTime(params.modificationTime);
}

void QtCodeFileList::addFile(std::shared_ptr<TokenLocationFile> locationFile, int refCount, TimePoint modificationTime)
{
	QtCodeFile* file = getFile(locationFile->getFilePath());
	file->setLocationFile(locationFile, refCount);
	file->setModificationTime(modificationTime);
}

void QtCodeFileList::clearCodeSnippets()
{
	m_files.clear();
	this->verticalScrollBar()->setValue(0);
}

const std::vector<Id>& QtCodeFileList::getActiveTokenIds() const
{
	return m_activeTokenIds;
}

void QtCodeFileList::setActiveTokenIds(const std::vector<Id>& activeTokenIds)
{
	m_activeTokenIds = activeTokenIds;
	m_activeLocalSymbolIds.clear();
}

const std::vector<Id>& QtCodeFileList::getActiveLocalSymbolIds() const
{
	return m_activeLocalSymbolIds;
}

void QtCodeFileList::setActiveLocalSymbolIds(const std::vector<Id>& activeLocalSymbolIds)
{
	m_activeLocalSymbolIds = activeLocalSymbolIds;
}

const std::vector<Id>& QtCodeFileList::getFocusedTokenIds() const
{
	return m_focusedTokenIds;
}

void QtCodeFileList::setFocusedTokenIds(const std::vector<Id>& focusedTokenIds)
{
	m_focusedTokenIds = focusedTokenIds;
}

std::vector<std::string> QtCodeFileList::getErrorMessages() const
{
	std::vector<std::string> errorMessages;
	for (const ErrorInfo& error : m_errorInfos)
	{
		errorMessages.push_back(error.message);
	}
	return errorMessages;
}

void QtCodeFileList::setErrorInfos(const std::vector<ErrorInfo>& errorInfos)
{
	m_errorInfos = errorInfos;
}

bool QtCodeFileList::hasErrors() const
{
	return m_errorInfos.size() > 0;
}

size_t QtCodeFileList::getFatalErrorCountForFile(const FilePath& filePath) const
{
	size_t fatalErrorCount = 0;
	for (const ErrorInfo& error : m_errorInfos)
	{
		if (error.filePath == filePath && error.isFatal)
		{
			fatalErrorCount++;
		}
	}
	return fatalErrorCount;
}

void QtCodeFileList::showActiveTokenIds()
{
	updateFiles();
}

void QtCodeFileList::showFirstActiveSnippet(bool scrollTo)
{
	updateFiles();

	QtCodeSnippet* snippet = getFirstActiveSnippet();

	if (!snippet)
	{
		expandActiveSnippetFile(scrollTo);
		return;
	}

	if (!snippet->isVisible())
	{
		snippet->getFile()->setSnippets();
	}

	if (scrollTo)
	{
		emit shouldScrollToSnippet(snippet, 0);
	}
}

void QtCodeFileList::focusTokenIds(const std::vector<Id>& focusedTokenIds)
{
	setFocusedTokenIds(focusedTokenIds);
	updateFiles();
}

void QtCodeFileList::defocusTokenIds()
{
	setFocusedTokenIds(std::vector<Id>());
	updateFiles();
}

void QtCodeFileList::setFileMinimized(const FilePath path)
{
	QtCodeFile* file = getFile(path);
	if (file)
	{
		file->setMinimized();
	}
}

void QtCodeFileList::setFileSnippets(const FilePath path)
{
	QtCodeFile* file = getFile(path);
	if (file)
	{
		file->setSnippets();
	}
}

void QtCodeFileList::setFileMaximized(const FilePath path)
{
	QtCodeFile* file = getFile(path);
	if (file)
	{
		file->setMaximized();
	}
}

void QtCodeFileList::updateFiles()
{
	for (std::shared_ptr<QtCodeFile> file: m_files)
	{
		file->updateContent();
	}
}

void QtCodeFileList::showContents()
{
	for (std::shared_ptr<QtCodeFile> filePtr : m_files)
	{
		filePtr->show();
	}
}

void QtCodeFileList::scrollToValue(int value)
{
	m_value = value;
	QTimer::singleShot(100, this, SLOT(setValue()));
}

void QtCodeFileList::scrollToActiveFileIfRequested()
{
	if (m_scrollToFile && m_scrollToFile->hasSnippets())
	{
		showFirstActiveSnippet(true);
		m_scrollToFile = nullptr;
	}
}

void QtCodeFileList::scrolled(int value)
{
	MessageScrollCode(value).dispatch();
}

void QtCodeFileList::scrollToSnippet(QtCodeSnippet* snippet, uint lineNumber)
{
	if (lineNumber == 0)
	{
		lineNumber = snippet->getFirstActiveLineNumber();
	}

	if (lineNumber)
	{
		this->ensureWidgetVisibleAnimated(snippet, snippet->getLineRectForLineNumber(lineNumber));
	}
}

void QtCodeFileList::setValue()
{
	this->verticalScrollBar()->setValue(m_value);
}

QtCodeFile* QtCodeFileList::getFile(const FilePath filePath)
{
	QtCodeFile* file = nullptr;

	for (std::shared_ptr<QtCodeFile> filePtr : m_files)
	{
		if (filePtr->getFilePath() == filePath)
		{
			file = filePtr.get();
			break;
		}
	}

	if (!file)
	{
		std::shared_ptr<QtCodeFile> filePtr = std::make_shared<QtCodeFile>(filePath, this);
		m_files.push_back(filePtr);

		file = filePtr.get();
		m_frame->layout()->addWidget(file);

		file->hide();
	}

	return file;
}

QtCodeSnippet* QtCodeFileList::getFirstActiveSnippet() const
{
	QtCodeSnippet* snippet = nullptr;
	for (std::shared_ptr<QtCodeFile> file: m_files)
	{
		snippet = file->findFirstActiveSnippet();
		if (snippet)
		{
			break;
		}
	}

	return snippet;
}

void QtCodeFileList::expandActiveSnippetFile(bool scrollTo)
{
	for (std::shared_ptr<QtCodeFile> file: m_files)
	{
		if (file->isCollapsedActiveFile())
		{
			file->requestSnippets();

			if (scrollTo)
			{
				m_scrollToFile = file.get();
			}

			return;
		}
	}
}

void QtCodeFileList::ensureWidgetVisibleAnimated(QWidget *childWidget, QRectF rect)
{
	if (!widget()->isAncestorOf(childWidget))
	{
		return;
	}

	const QRect microFocus = childWidget->inputMethodQuery(Qt::ImCursorRectangle).toRect();
	const QRect defaultMicroFocus = childWidget->QWidget::inputMethodQuery(Qt::ImCursorRectangle).toRect();
	QRect focusRect = (microFocus != defaultMicroFocus)
		? QRect(childWidget->mapTo(widget(), microFocus.topLeft()), microFocus.size())
		: QRect(childWidget->mapTo(widget(), QPoint(0, 0)), childWidget->size());
	const QRect visibleRect(-widget()->pos(), viewport()->size());

	if (rect.height() > 0)
	{
		focusRect = QRect(childWidget->mapTo(widget(), rect.topLeft().toPoint()), rect.size().toSize());
		focusRect.adjust(0, 0, 0, 100);
	}

	QScrollBar* scrollBar = verticalScrollBar();
	int value = focusRect.center().y() - visibleRect.center().y();

	if (scrollBar && value != 0)
	{
		QPropertyAnimation* anim = new QPropertyAnimation(scrollBar, "value");
		anim->setDuration(300);
		anim->setStartValue(scrollBar->value());
		anim->setEndValue(scrollBar->value() + value);
		anim->setEasingCurve(QEasingCurve::InOutQuad);
		anim->start();
	}
}
