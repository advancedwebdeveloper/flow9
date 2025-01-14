#include "DebugManager.hpp"

#include <QRegExp>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QDebug>
#include <QFileInfo>

#include <KMessageBox>
#include <KLocalizedString>
#include <KTextEditor/View>

#include <signal.h>

#include "FlowView.hpp"

namespace flow {

DebugManager::DebugManager(FlowView& view)
:   QObject(&view), flowView_(view), debugLocationChanged_(true), currentFrame_(0) {
}

DebugManager::~DebugManager() {
    if (debugProcess_.state() != QProcess::NotRunning) {
        debugProcess_.kill();
        debugProcess_.blockSignals(true);
        debugProcess_.waitForFinished();
    }
}

void DebugManager::runDebugger(const DebugConf &conf) {
	try {
		debugConf_ = conf;
		if (debugProcess_.state() == QProcess::NotRunning) {
			connect(&debugProcess_, SIGNAL(error(QProcess::ProcessError)), this, SLOT(slotError(QProcess::ProcessError)));
			connect(&debugProcess_, SIGNAL(readyReadStandardError()), this, SLOT(slotReadDebugStdErr()));
			connect(&debugProcess_, SIGNAL(readyReadStandardOutput()), this, SLOT(slotReadDebugStdOut()));
			connect(&debugProcess_, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotDebugFinished(int, QProcess::ExitStatus)));
			connect(flowView_.flowOutput_.ui.terminateCompilerButton, SIGNAL(clicked()), &debugProcess_, SLOT(terminate()));

			QStringList args;
			args << QLatin1String("--debug-mi");
			args << debugConf_.executable;
			args << debugConf_.debuginfo;
			args << QLatin1String("--");
			args << debugConf_.arguments.split(QLatin1Char(' '));

			QTextStream(stdout) << "DEBUG: " << debugConf_.fdbCmd.trimmed() << " " << args.join(QLatin1Char(' ')) << "\n";

			flowView_.flowOutput_.ui.launchOutTextEdit->clear();
			flowView_.flowOutput_.ui.debugOutTextEdit->clear();

			debugProcess_.setWorkingDirectory(debugConf_.workDir);
			debugProcess_.start(debugConf_.fdbCmd.trimmed(), args);
			flowView_.flowOutput_.ui.terminateDebugButton->setEnabled(true);
		} else {
			// On startup the fdb prompt will trigger the "nextCommands",
			// here we have to trigger it manually.
			QTimer::singleShot(0, this, SLOT(slotIssueNextCommand()));
		}
		for (auto bp : breakPointList_) {
			nextCommands_ << QStringLiteral("-break-insert %1:%2").arg(bp.file.path()).arg(bp.line);
		}
		nextCommands_ << QLatin1String("-exec-run");
	} catch (std::exception& ex) {
		debugProcess_.kill();
		KMessageBox::sorry(0, QLatin1String(ex.what()));
	}
}

void DebugManager::slotDebug(int row) {
	try {
		DebugConf debugConf;
		debugConf.executable = flowView_.flowConfig_.ui.launchTableWidget->item(row, 1)->text();
		debugConf.workDir    = flowView_.flowConfig_.ui.launchTableWidget->item(row, 2)->text();
		debugConf.arguments  = flowView_.flowConfig_.ui.launchTableWidget->item(row, 5)->text();
		debugConf.fdbCmd     = QFileInfo(QLatin1String("flowcpp")).absoluteFilePath();
		QFileInfo info(debugConf.executable);
		debugConf.executable = info.dir().path() + QDir::separator() + info.baseName() + QLatin1String(".bytecode");
		debugConf.debuginfo = info.dir().path() + QDir::separator() + info.baseName() + QLatin1String(".debug");
		runDebugger(debugConf);
	} catch (std::exception& ex) {
		debugProcess_.kill();
		KMessageBox::sorry(0, QLatin1String(ex.what()));
	}
}

bool DebugManager::hasBreakpoint(const QUrl& url, int line) const {
    return breakpointIndex(url, line) != -1;
}

int DebugManager::breakpointIndex(const QUrl& url, int line) const {
    for (int i = 0; i < breakPointList_.size(); i++) {
        if ((url == breakPointList_[i].file) && (line == breakPointList_[i].line)) {
            return i;
        }
    }
    return -1;
}

void DebugManager::toggleBreakpoint(const QUrl& url, int line) {
    if (debugProcess_.state() == QProcess::Running) {
        QString cmd;
        if (hasBreakpoint(url, line)) {
            cmd = QStringLiteral("-break-delete %1:%2").arg(url.path()).arg(line);
        } else {
            cmd = QStringLiteral("-break-insert %1:%2").arg(url.path()).arg(line);
        }
        issueCommand(cmd);
    } else {
    	int ind = breakpointIndex(url, line);
    	if (ind != -1) {
			breakPointList_.removeAt(ind);
			emit signalBreakPointCleared(url, line -1);
    	} else {
			breakPointList_ << BreakPoint {breakPointList_.count(), url, line};
			emit signalBreakPointSet(url, line -1);
    	}
    }
}

void DebugManager::stackFrameSelected(int level) {
	nextCommands_ << QLatin1String("-stack-select-frame ") + QString::number(level);
	nextCommands_ << QLatin1String("-stack-list-arguments --all-values");
	nextCommands_ << QLatin1String("-stack-list-locals --all-values");
	currentFrame_ = level;
	slotIssueNextCommand();
}

void DebugManager::slotError(QProcess::ProcessError err) {
    KMessageBox::sorry(NULL, i18n("Error in debugger process: ") + debugProcess_.errorString());
}

inline QString prepareOutput(const QString& line) {
	return line.mid(2,line.length() - 3).replace(QLatin1String("\\n"), QLatin1String("\n"));
}

void DebugManager::slotReadDebugStdOut() {
	static QRegExp nonemptyString(QLatin1String("^(?!\\s*$).+"));
	QString out = QString::fromLocal8Bit(debugProcess_.readAllStandardOutput().data());
	//QTextStream(stdout) << "OUT:\n" << out << "\n-----\n";
    QStringList lines = out.split(QLatin1Char('\n')).filter(nonemptyString);
    for (auto line : lines) {
    	if (line.startsWith(QLatin1Char('@'))) {
    		appendText(flowView_.flowOutput_.ui.launchOutTextEdit, prepareOutput(line));
    		flowView_.flowOutput_.ui.tabWidget->setCurrentIndex(1);
    	} else if (line.startsWith(QLatin1Char('~'))) {
    		appendText(flowView_.flowOutput_.ui.debugOutTextEdit, prepareOutput(line));
    		flowView_.flowOutput_.ui.tabWidget->setCurrentIndex(2);
    	} else if (line.startsWith(QLatin1Char('&'))) {
    		// TODO: handle log output
    	} else if (!line.isEmpty()) {
    		processLine(line);
    	}
    }
}

void DebugManager::slotReadDebugStdErr() {
	QString out = QString::fromLocal8Bit(debugProcess_.readAllStandardError().data());
	//QTextStream(stdout) << "ERROR:\n" << out << "\n-----\n";
	appendText(flowView_.flowOutput_.ui.debugOutTextEdit, prepareOutput(out));
    flowView_.flowOutput_.ui.tabWidget->setCurrentIndex(2);
}

void DebugManager::slotDebugFinished(int exitCode, QProcess::ExitStatus status) {
	flowView_.flowOutput_.ui.terminateDebugButton->setEnabled(false);
    if (status != QProcess::NormalExit) {
        QString message = i18n("*** fdb crashed *** ") + debugProcess_.errorString();
    	appendText(flowView_.flowOutput_.ui.debugOutTextEdit, message);
    	flowView_.flowOutput_.ui.tabWidget->setCurrentIndex(2);
    	KMessageBox::sorry(flowView_.mainWindow_->activeView(), message);
    }
}

void DebugManager::slotInterrupt() {
    if (debugProcess_.state() == QProcess::Running) {
        debugLocationChanged_ = true;
    }
    int pid = debugProcess_.pid();
    if (pid != 0) {
        ::kill(pid, SIGINT);
    }
}

void DebugManager::slotKill() {
    if (debugProcess_.state() == QProcess::Running) {
        slotInterrupt();
        issueCommand(QLatin1String("quit"));
    }
}

void DebugManager::slotStepInto() {
    issueCommand(QLatin1String("-exec-step"));
}

void DebugManager::slotStepOver() {
    issueCommand(QLatin1String("-exec-next"));
}

void DebugManager::slotStepOut() {
    issueCommand(QLatin1String("-exec-finish"));
}

void DebugManager::slotContinue() {
    issueCommand(QLatin1String("-exec-continue"));
}

struct PromptHandler : public DebugManager::LineHandler { // @suppress("Class has a virtual method and non-virtual destructor")
	PromptHandler() : DebugManager::LineHandler(
		QLatin1String("\\([gf]db\\)\\s*")
	) {}
	void handle(const QString& line, DebugManager* manager) override {
		QTimer::singleShot(0, manager, SLOT(slotIssueNextCommand()));
	}
};

struct BreakpointHandler : public DebugManager::LineHandler { // @suppress("Class has a virtual method and non-virtual destructor")
	BreakpointHandler() : DebugManager::LineHandler(
		QLatin1String("\\^done,bkpt=\\{.*number=\"(\\d+)\",.*fullname=\"([^\"]*)\",.*line=\"(\\d+)\".*")
	) {}
	void handle(const QString& line, DebugManager* manager) override {
		DebugManager::BreakPoint breakPoint;
		breakPoint.number = lineMatcher.cap(1).toInt();
		breakPoint.file = manager->resolveFileName(lineMatcher.cap(2));
		breakPoint.line = lineMatcher.cap(3).toInt();
		if (!manager->hasBreakpoint(breakPoint.file, breakPoint.line)) {
			manager->breakPointList_ << breakPoint;
		}
		emit manager->signalBreakPointSet(breakPoint.file, breakPoint.line -1);
	}
};

struct FrameStoppedHandler : public DebugManager::LineHandler { // @suppress("Class has a virtual method and non-virtual destructor")
	FrameStoppedHandler() : DebugManager::LineHandler(
		QLatin1String("\\*stopped,frame=\\{.*func=\"([^\"]*)\",.*fullname=\"([^\"]*)\",.*line=\"(\\d+)\".*")
	) {}
	void handle(const QString& line, DebugManager* manager) override {
		QString func = lineMatcher.cap(1);
		QString file = lineMatcher.cap(2);
		int lineNum = lineMatcher.cap(3).toInt();
		if (!manager->nextCommands_.contains(QLatin1String("-exec-continue"))) {
			// fdb uses 1 based line numbers, kate uses 0 based...
			emit manager->signalDebugLocationChanged(manager->resolveFileName(file), lineNum - 1);
		}
		manager->debugLocationChanged_ = true;
	}
};

struct StackLocalsHandler : public DebugManager::LineHandler { // @suppress("Class has a virtual method and non-virtual destructor")
	StackLocalsHandler() : DebugManager::LineHandler(
		QLatin1String("\\^done,(locals=\\[.*\\])")
	) {}
	void handle(const QString& line, DebugManager* manager) override {
		emit manager->signalLocalsInfo(lineMatcher.cap(1));
	}
};

struct StackFramesHandler : public DebugManager::LineHandler { // @suppress("Class has a virtual method and non-virtual destructor")
	StackFramesHandler() : DebugManager::LineHandler(
		QLatin1String("\\^done,(stack=\\[.*\\])")
	) {}
	void handle(const QString& line, DebugManager* manager) override {
		emit manager->signalStackInfo(lineMatcher.cap(1));
	}
};

struct StackArgsHandler : public DebugManager::LineHandler { // @suppress("Class has a virtual method and non-virtual destructor")
	StackArgsHandler() : DebugManager::LineHandler(
		QLatin1String("\\^done,(stack-args=\\[.*\\])")
	) {}
	void handle(const QString& line, DebugManager* manager) override {
		emit manager->signalArgsInfo(lineMatcher.cap(1), manager->currentFrame_);
	}
};

void DebugManager::processLine(QString line) {
	static DebugManager::LineHandler* handlers[] = {
		new PromptHandler,      new BreakpointHandler,  new FrameStoppedHandler,
		new StackLocalsHandler, new StackFramesHandler, new StackArgsHandler
	};
	for (auto h : handlers) {
		if (h->lineMatcher.exactMatch(line)) {
			h->handle(line, this);
			break;
		}
	}
}

void DebugManager::issueCommand(QString const& cmd) {
	lastCommand_ = cmd;
	//QTextStream (stdout) << "COMMAND:\n" << lastCommand_ << "\n-------------------\n\n";
	debugProcess_.write(qPrintable(cmd));
	debugProcess_.write("\n");
}

void DebugManager::slotIssueNextCommand() {
	if (nextCommands_.size() > 0) {
		QString cmd = nextCommands_.takeFirst();
		issueCommand(cmd);
	} else if (debugLocationChanged_) {
		debugLocationChanged_ = false;
		queryLocals();
		return;
	}
}

QUrl DebugManager::resolveFileName(const QString &fileName) {
    QUrl url;

    QFileInfo fInfo = QFileInfo(fileName);
    //did we end up with an absolute path or a relative one?
    if (fInfo.exists()) {
        return QUrl::fromUserInput(fInfo.absoluteFilePath());
    }
    if (fInfo.isAbsolute()) {
        // we can not do anything just return the fileName
        return QUrl::fromUserInput(fileName);
    }
    // Now try to add the working path
    fInfo = QFileInfo(debugConf_.workDir + fileName);
    if (fInfo.exists()) {
        return QUrl::fromUserInput(fInfo.absoluteFilePath());
    }
    // now try the executable path
    fInfo = QFileInfo(QFileInfo(debugConf_.executable).absolutePath() + fileName);
    if (fInfo.exists()) {
        return QUrl::fromUserInput(fInfo.absoluteFilePath());
    }
    for (QString srcPath : debugConf_.srcPaths) {
        fInfo = QFileInfo(srcPath + QDir::separator() + fileName);
        if (fInfo.exists()) {
            return QUrl::fromUserInput(fInfo.absoluteFilePath());
        }
    }
    QTextStream (stdout) << "UNABLE TO RESOLVEl: " << fileName << "\n";
    // we can not do anything just return the fileName
    return QUrl::fromUserInput(fileName);
}

void DebugManager::queryLocals() {
	nextCommands_ << QLatin1String("-stack-list-arguments --all-values");
	nextCommands_ << QLatin1String("-stack-list-locals --all-values");
	nextCommands_ << QLatin1String("-stack-list-frames");
	currentFrame_ = 0;
	slotIssueNextCommand();
}

void DebugManager::writeConfig(KConfigGroup& config) const {
    config.writeEntry(QLatin1String("Breakpoints number"), breakPointList_.count());
    int i = 0;
    for (auto bp : breakPointList_) {
    	config.writeEntry(QStringLiteral("Breakpoint %1 number").arg(i), bp.number);
    	config.writeEntry(QStringLiteral("Breakpoint %1 file").arg(i), bp.file);
    	config.writeEntry(QStringLiteral("Breakpoint %1 line").arg(i), bp.line);
    	++i;
    }
}

void DebugManager::readConfig(const KConfigGroup& config) {
	for (int i = 0; i < config.readEntry(QLatin1String("Breakpoints number"), 0); ++i) {
		BreakPoint bp;
		bp.number = config.readEntry(QStringLiteral("Breakpoint %1 number").arg(i), -1);
		bp.line = config.readEntry(QStringLiteral("Breakpoint %1 line").arg(i), -1);
		bp.file = config.readEntry(QStringLiteral("Breakpoint %1 file").arg(i), QUrl());
		if (!hasBreakpoint(bp.file, bp.line)) {
			breakPointList_ << bp;
			emit signalBreakPointSet(bp.file, bp.line -1);
		}
	}
}

void DebugManager::eraseConfig(KConfigGroup& config) {
	for (int i = 0; i < config.readEntry(QLatin1String("Breakpoints number"), 0); ++i) {
		config.deleteEntry(QStringLiteral("Breakpoint %1 number").arg(i));
		config.deleteEntry(QStringLiteral("Breakpoint %1 line").arg(i));
		config.deleteEntry(QStringLiteral("Breakpoint %1 file").arg(i));
	}
	config.deleteEntry(QLatin1String("Breakpoints number"));
}

}
