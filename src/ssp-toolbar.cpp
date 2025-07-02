#include "ssp-toolbar.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QCloseEvent>
#include <QToolButton>
#include <QPointer>
#include <browser-panel.hpp>
#include <QTimer>
#include <random>
#include <QThread>
#include <QMouseEvent>
#include <QWindow>
#include <QApplication>

SspToolbarManager *SspToolbarManager::s_instance = nullptr;
QCef *SspToolbarManager::m_qcef = nullptr;
QCefCookieManager *SspToolbarManager::panel_cookies = nullptr;

SspToolbarManager *SspToolbarManager::instance()
{
	if (!s_instance) {
		if (QApplication::instance() &&
		    QThread::currentThread() !=
			    QApplication::instance()->thread()) {
			QMetaObject::invokeMethod(
				QApplication::instance(),
				[]() {
					if (!s_instance) {
						s_instance =
							new SspToolbarManager();
					}
				},
				Qt::BlockingQueuedConnection);
		} else {
			if (!s_instance) {
				s_instance = new SspToolbarManager();
			}
		}
	}
	return s_instance;
}

static std::string GenId()
{
	std::random_device rd;
	std::mt19937_64 e2(rd());
	std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFF);

	uint64_t id = dist(e2);

	char id_str[20];
	snprintf(id_str, sizeof(id_str), "%16llX", (unsigned long long)id);
	return std::string(id_str);
}

SspToolbarManager::SspToolbarManager(QObject *parent)
	: QObject(parent),
	  m_toolbar(nullptr),
	  m_mainWindow(nullptr),
	  m_browserInitialized(false)
{
	m_mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	m_initTimer = new QTimer(this);
	connect(m_initTimer, &QTimer::timeout, this,
		&SspToolbarManager::checkBrowserInitialization);

	connect(this, &SspToolbarManager::addSourceActionRequested, this,
		&SspToolbarManager::doAddSourceAction, Qt::QueuedConnection);
	connect(this, &SspToolbarManager::removeSourceActionRequested, this,
		&SspToolbarManager::doRemoveSourceAction, Qt::QueuedConnection);
	// Connect to QApplication::activeWindowChanged
#if defined(__APPLE__)
	connect(QApplication::instance(), SIGNAL(focusWindowChanged(QWindow *)),
		this, SLOT(onActiveWindowChanged(QWindow *)));
#endif
	initializeBrowserPanel();
}

SspToolbarManager::~SspToolbarManager()
{
	if (m_initTimer) {
		m_initTimer->stop();
		m_initTimer->deleteLater();
	}

	m_browserDocks.clear();

	if (panel_cookies) {
		panel_cookies->FlushStore();
		delete panel_cookies;
		panel_cookies = nullptr;
	}
	if (m_qcef) {
		delete m_qcef;
		m_qcef = nullptr;
	}
}
// New slot to handle active window changes
void SspToolbarManager::onActiveWindowChanged(QWindow *window)
{
	if (window && window != m_mainWindow->windowHandle()) {
		// Check if the active window is not the main window or one of our docks
		bool isDock = false;
		for (const auto &dock : m_browserDocks) {
			if (dock && dock->windowHandle() == window) {
				isDock = true;
				break;
			}
		}
		if (!isDock) {
			// Hide all docks
			for (auto &dock : m_browserDocks) {
				if (dock && dock->isVisible()) {
					dock->hide();
					// Update the corresponding action
					for (auto it = m_sourceActions.begin();
					     it != m_sourceActions.end();
					     ++it) {
						if (m_browserDocks[it.key()] ==
						    dock) {
							QAction *action =
								it.value();
							if (action->isChecked()) {
								action->setChecked(
									false);
							}
							break;
						}
					}
				}
			}
		}
	}
}

void SspToolbarManager::addSourceAction(const QString &sourceName,
					const QString &ip)
{
	emit addSourceActionRequested(sourceName, ip);
}

void SspToolbarManager::removeSourceAction(const QString &sourceName,
					   const QString &ip)
{
	emit removeSourceActionRequested(sourceName, ip);
}

void SspToolbarManager::doAddSourceAction(const QString &sourceName,
					  const QString &ip)
{
	if (!m_toolbar) {
		createToolbar();
	}

	QString sourceKey = QString("%1_%2").arg(sourceName).arg(ip);

	if (!m_sourceActions.contains(sourceKey)) {
		blog(LOG_INFO, "Adding source action in toolbar: %s",
		     sourceKey.toStdString().c_str());
		QAction *action = new QAction(sourceName, this);
		action->setCheckable(true);
		action->setProperty("themeID", "sspToolbarButton");

		connect(action, &QAction::triggered, this, [this, action]() {
			for (QToolButton *btn :
			     m_toolbar->findChildren<QToolButton *>()) {
				if (btn->defaultAction() == action) {
					btn->setProperty("sspActionButton",
							 true);
					btn->setStyleSheet(
						"QToolButton:checked { background-color: #5865F2; color: white; border-radius: 2px; }");
					break;
				}
			}
		});

		connect(action, &QAction::toggled, this,
			[this, sourceName, ip, sourceKey](bool checked) {
				bool dockExists =
					m_browserDocks.contains(sourceKey);
				bool dockVisible =
					dockExists &&
					m_browserDocks[sourceKey] &&
					m_browserDocks[sourceKey]->isVisible();

				if (checked != dockVisible) {
					if (checked) {
						if (!dockExists ||
						    !m_browserDocks[sourceKey]) {
							showBrowserDock(
								sourceName, ip,
								sourceKey);
						} else if (m_browserDocks
								   [sourceKey]) {
							QDockWidget *dock =
								m_browserDocks
									[sourceKey];
							blog(LOG_INFO,
							     "Restoring dock visibility for %s",
							     sourceKey
								     .toStdString()
								     .c_str());
							dock->setVisible(true);
							dock->raise();
							dock->activateWindow();
						}
					} else if (dockExists &&
						   m_browserDocks[sourceKey]) {
						blog(LOG_INFO,
						     "Hiding dock for %s",
						     sourceKey.toStdString()
							     .c_str());
						m_browserDocks[sourceKey]
							->setVisible(false);
					}
				}
			});

		m_sourceActions[sourceKey] = action;
		if (m_toolbar) {
			m_toolbar->addAction(action);
		}
	}
}

void SspToolbarManager::doRemoveSourceAction(const QString &sourceName,
					     const QString &ip)
{
	QString sourceKey = QString("%1_%2").arg(sourceName).arg(ip);
	blog(LOG_INFO, "Removing source action in toolbar: %s",
	     sourceKey.toStdString().c_str());
	if (m_sourceActions.contains(sourceKey)) {
		QAction *action = m_sourceActions[sourceKey];
		if (m_toolbar != nullptr)
			m_toolbar->removeAction(action);
		delete action;
		m_sourceActions.remove(sourceKey);
	}

	if (m_browserDocks.contains(sourceKey)) {
		QDockWidget *dock = m_browserDocks.take(sourceKey);
		if (dock != nullptr) {
			QCefWidget *w = (QCefWidget *)(dock->widget());
			if (w != nullptr) {
				w->closeBrowser();
			}
			if (m_mainWindow != nullptr)
				m_mainWindow->removeDockWidget(dock);
			dock->deleteLater();
		}
	}

	if (m_toolbar && m_sourceActions.isEmpty()) {
		removeToolbar();
	}
}

void SspToolbarManager::createToolbar()
{
	if (QThread::currentThread() != thread()) {
		QMetaObject::invokeMethod(this, "createToolbar",
					  Qt::QueuedConnection);
		return;
	}
	if (!m_toolbar) {
		m_toolbar =
			new QToolBar(obs_module_text("SSPPlugin.Toolbar.Title"),
				     m_mainWindow);
		m_toolbar->setObjectName("sspToolbar");
		m_toolbar->setProperty("themeID", "sspToolbar");
		QString styleSheet = "QToolBar::separator { width: 2px; }";
		styleSheet +=
			"QToolButton:checked { background-color: rgb(88, 101, 242); border-radius: 2px; }";
		m_toolbar->setStyleSheet(styleSheet);
		m_mainWindow->addToolBar(m_toolbar);
	}
}

void SspToolbarManager::removeToolbar()
{
	if (QThread::currentThread() != thread()) {
		QMetaObject::invokeMethod(this, "removeToolbar",
					  Qt::QueuedConnection);
		return;
	}
	if (m_toolbar) {
		if (m_mainWindow) {
			m_mainWindow->removeToolBar(m_toolbar);
		}
		m_toolbar->deleteLater();
		m_toolbar = nullptr;
	}
}

void SspToolbarManager::initializeBrowserPanel()
{
	if (!m_qcef) {
		m_qcef = obs_browser_init_panel();
		if (m_qcef) {
			m_qcef->init_browser();
			m_initTimer->start(100);
		} else {
			blog(LOG_ERROR, "Failed to initialize browser panel");
		}
	}
}

void SspToolbarManager::checkBrowserInitialization()
{
	if (m_qcef && m_qcef->initialized()) {
		m_browserInitialized = true;
		m_initTimer->stop();

		if (!panel_cookies) {
			std::string sub_path;
			sub_path += "imvt/";
			sub_path += GenId();
			panel_cookies = m_qcef->create_cookie_manager(sub_path);
		}

		processPendingDocks();

		blog(LOG_INFO, "Browser panel initialized successfully");
	}
}

void SspToolbarManager::processPendingDocks()
{
	for (auto it = m_browserDocks.begin(); it != m_browserDocks.end();
	     ++it) {
		QDockWidget *dock = it.value();
		if (dock && !dock->widget()) {
			QString sourceKey = it.key();
			QString sourceName = dock->windowTitle();
			QString ip = sourceKey.split('_').last();
			createBrowserWidget(dock, sourceName, ip);
		}
	}
}

void SspToolbarManager::suppressUnloadDialog(QCefWidget *widget)
{
	if (widget) {
		blog(LOG_INFO, "Suppressing unload dialog via JavaScript.");
		widget->executeJavaScript("window.onbeforeunload = null;");
	}
}

void SspToolbarManager::scheduleSuppressUnloadDialog(const QString &sourceKey)
{
	QTimer::singleShot(1000, this, [this, sourceKey]() {
		if (!m_browserDocks.contains(sourceKey)) {
			blog(LOG_WARNING,
			     "Dock for key %s no longer exists, cannot suppress unload dialog.",
			     sourceKey.toStdString().c_str());
			return;
		}
		QDockWidget *dock = m_browserDocks.value(sourceKey);
		if (dock && dock->widget()) {
			QCefWidget *browser = (QCefWidget *)(dock->widget());
			if (browser) {
				suppressUnloadDialog(browser);
			} else {
				blog(LOG_WARNING,
				     "No QCefWidget found in dock for %s.",
				     sourceKey.toStdString().c_str());
			}
		} else {
			blog(LOG_WARNING, "Dock or its widget is null for %s.",
			     sourceKey.toStdString().c_str());
		}
	});
}

QCefWidget *SspToolbarManager::createBrowserWidget(QDockWidget *dock,
						   const QString &sourceName,
						   const QString &ip)
{
	if (!m_qcef || !m_qcef->initialized() || !panel_cookies) {
		blog(LOG_WARNING,
		     "Cannot create browser widget: CEF not initialized or cookies missing");
		return nullptr;
	}
	std::string url = "http://" + ip.toStdString();
	blog(LOG_INFO, "Creating browser widget with URL: %s", url.c_str());
	QCefWidget *cefWidget = m_qcef->create_widget(dock, url, panel_cookies);
	if (cefWidget) {
		cefWidget->setStartupScript("window.onbeforeunload = null;");
		dock->setWidget(cefWidget);
		cefWidget->setVisible(true);
		cefWidget->setFocus();
		blog(LOG_INFO, "Browser widget %p created for %s",
		     (void *)cefWidget, sourceName.toStdString().c_str());
	}
	return cefWidget;
}

QDockWidget *SspToolbarManager::createBrowserDock(const QString &sourceName,
						  const QString &ip,
						  const QString &sourceKey)
{
	QDockWidget *dock = new QDockWidget(sourceName, m_mainWindow);
	dock->setObjectName("sspDock_" + sourceKey);

	// Disable docking to prevent drag-to-edge docking
	dock->setFeatures(QDockWidget::DockWidgetFloatable);
	dock->setAllowedAreas(Qt::NoDockWidgetArea);

	// Custom title bar with hide button
	QWidget *titleBar = new QWidget(dock);
	QHBoxLayout *layout = new QHBoxLayout(titleBar);
	layout->setContentsMargins(5, 0, 0, 0);
	layout->setSpacing(0);

	QLabel *titleLabel = new QLabel(sourceName, titleBar);
	titleLabel->setStyleSheet("font-weight: bold;");

	QToolButton *hideButton = new QToolButton(titleBar);
	hideButton->setText("X");
	hideButton->setAutoRaise(true);
	hideButton->setToolTip(tr("Hide"));

	connect(hideButton, &QToolButton::clicked, dock, &QDockWidget::hide);

	layout->addWidget(titleLabel);
	layout->addStretch();
	layout->addWidget(hideButton);

	// Install event filter on title bar to suppress double-click
	titleBar->installEventFilter(this);

	dock->setTitleBarWidget(titleBar);

	dock->resize(1280, 720);
	dock->setMinimumSize(600, 480);
	dock->setWindowTitle(sourceName);
	dock->setFloating(true);
	dock->setProperty("themeID", "sspDockTheme");

	if (m_browserInitialized) {
		createBrowserWidget(dock, sourceName, ip);
	}

	m_mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);

	blog(LOG_INFO, "Created dock %s with geometry: %d,%d,%d,%d",
	     sourceKey.toStdString().c_str(), dock->geometry().x(),
	     dock->geometry().y(), dock->geometry().width(),
	     dock->geometry().height());

	dock->show();
	dock->raise();
	dock->activateWindow();

	if (m_sourceActions.contains(sourceKey)) {
		if (m_sourceActions[sourceKey]->isChecked() != true) {
			m_sourceActions[sourceKey]->setChecked(true);
		}
	}

	return dock;
}

void SspToolbarManager::showBrowserDock(const QString &sourceName,
					const QString &ip,
					const QString &sourceKey)
{
	if (!m_browserDocks.contains(sourceKey) ||
	    !m_browserDocks.value(sourceKey)) {
		QDockWidget *dock =
			createBrowserDock(sourceName, ip, sourceKey);
		m_browserDocks[sourceKey] = dock;

		connect(dock, &QDockWidget::visibilityChanged, this,
			[this, sourceName, ip, sourceKey](bool visible) {
				if (m_sourceActions.contains(sourceKey)) {
					QAction *action =
						m_sourceActions[sourceKey];
					if (action->isChecked() != visible) {
						action->setChecked(visible);
					}
				}
				if (visible &&
				    m_browserDocks.contains(sourceKey)) {
					QDockWidget *currentDock =
						m_browserDocks.value(sourceKey);
					if (currentDock &&
					    currentDock->widget()) {
						QCefWidget *browser = static_cast<
							QCefWidget *>(
							currentDock->widget());
						if (browser) {
							std::string url =
								"http://" +
								ip.toStdString();
							// Try setting startup script before changing URL
							browser->setStartupScript(
								"window.onbeforeunload = null;");
							browser->setURL(url);
							// No need for scheduleSuppressUnloadDialog if startup script works
						}
					} else if (currentDock &&
						   !currentDock->widget() &&
						   m_browserInitialized) {
						createBrowserWidget(currentDock,
								    sourceName,
								    ip);
					}
				}
			});
		dock->installEventFilter(this);
	} else {
		QDockWidget *dock = m_browserDocks.value(sourceKey);
		if (dock) {
			dock->setFloating(true);
			dock->show();
			dock->raise();
			dock->activateWindow();

			if (dock->widget()) {
				QCefWidget *browser = static_cast<QCefWidget *>(
					dock->widget());
				if (browser) {
					std::string url =
						"http://" + ip.toStdString();
					// Try setting startup script before changing URL
					browser->setStartupScript(
						"window.onbeforeunload = null;");
					browser->setURL(url);
					// No need for scheduleSuppressUnloadDialog if startup script works
					browser->setFocus();
				}
			} else if (m_browserInitialized) {
				createBrowserWidget(dock, sourceName, ip);
			}
		} else {
			m_browserDocks.remove(sourceKey);
			showBrowserDock(sourceName, ip, sourceKey);
		}
	}
}

bool SspToolbarManager::eventFilter(QObject *watched, QEvent *event)
{
	// Existing double-click handling
	if (event->type() == QEvent::MouseButtonDblClick) {
		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() == Qt::LeftButton) {
			for (const auto &dock : m_browserDocks) {
				if (dock && dock->titleBarWidget() == watched) {
					blog(LOG_INFO,
					     "Ignoring double-click on title bar for dock %s",
					     dock->objectName()
						     .toStdString()
						     .c_str());
					return true; // Consume the double-click event
				}
			}
		}
	}

	// Existing hide event handling
	if (event->type() == QEvent::Hide) {
		for (auto it = m_browserDocks.begin();
		     it != m_browserDocks.end(); ++it) {
			if (watched == it.value()) {
				QString sourceKey = it.key();
				blog(LOG_INFO, "Hide event for dock %s",
				     sourceKey.toStdString().c_str());
				if (m_sourceActions.contains(sourceKey)) {
					QAction *action =
						m_sourceActions[sourceKey];
					if (action->isChecked()) {
						action->setChecked(false);
					}
				}
				break;
			}
		}
	}
	// Existing show event handling
	else if (event->type() == QEvent::Show) {
		for (auto it = m_browserDocks.begin();
		     it != m_browserDocks.end(); ++it) {
			if (watched == it.value()) {
				QString sourceKey = it.key();
				blog(LOG_INFO, "Show event for dock %s",
				     sourceKey.toStdString().c_str());
				if (m_sourceActions.contains(sourceKey)) {
					QAction *action =
						m_sourceActions[sourceKey];
					if (!action->isChecked()) {
						action->setChecked(true);
					}
				}
				break;
			}
		}
	}
	// Existing close event handling
	else if (event->type() == QEvent::Close) {
		for (auto it = m_browserDocks.begin();
		     it != m_browserDocks.end(); ++it) {
			if (watched == it.value()) {
				QString sourceKey = it.key();
				blog(LOG_INFO, "Close event for %s",
				     sourceKey.toStdString().c_str());
				if (m_sourceActions.contains(sourceKey)) {
					QAction *action =
						m_sourceActions[sourceKey];
					if (action->isChecked()) {
						action->setChecked(false);
					}
				}
				m_browserDocks.remove(sourceKey);
				break;
			}
		}
	}

	return QObject::eventFilter(watched, event);
}

void SspToolbarManager::shutdown()
{
	blog(LOG_INFO, "[SSPToolbar] static shutdown() called.");
	if (!s_instance) {
		return;
	}

	if (QThread::currentThread() != s_instance->thread()) {
		QMetaObject::invokeMethod(s_instance, "shutdownInternal",
					  Qt::BlockingQueuedConnection);
	} else {
		s_instance->shutdownInternal();
	}
	blog(LOG_INFO,
	     "[SSPToolbar] static shutdown() finished. s_instance is now %s.",
	     s_instance ? "NOT NULL" : "NULL");
}

void SspToolbarManager::shutdownInternal()
{
	blog(LOG_INFO,
	     "[SSPToolbar] shutdownInternal: Starting cleanup on thread %p...",
	     (void *)QThread::currentThread());

	if (m_initTimer) {
		m_initTimer->stop();
	}

	blog(LOG_INFO,
	     "[SSPToolbar] shutdownInternal: Closing browser docks (%d docks)...",
	     m_browserDocks.size());
	for (QDockWidget *dock : qAsConst(m_browserDocks).values()) {
		if (dock) {
			QCefWidget *browser = (QCefWidget *)(dock->widget());
			if (browser) {
				browser->closeBrowser();
				QApplication::processEvents(
					QEventLoop::ProcessEventsFlag::
						ExcludeUserInputEvents,
					100);
			}
			if (m_mainWindow) {
				m_mainWindow->removeDockWidget(dock);
			}
			dock->deleteLater();
		}
	}
	m_browserDocks.clear();

	blog(LOG_INFO,
	     "[SSPToolbar] shutdownInternal: Cleaning up QActions (%d actions)...",
	     m_sourceActions.size());
	qDeleteAll(m_sourceActions.begin(), m_sourceActions.end());
	m_sourceActions.clear();

	if (m_toolbar) {
		if (m_mainWindow)
			m_mainWindow->removeToolBar(m_toolbar);
		m_toolbar->deleteLater();
		m_toolbar = nullptr;
	}

	if (panel_cookies) {
		panel_cookies->FlushStore();
		delete panel_cookies;
		panel_cookies = nullptr;
	}

	if (m_qcef) {
		QApplication::processEvents(
			QEventLoop::ProcessEventsFlag::ExcludeUserInputEvents,
			100);
		delete m_qcef;
		m_qcef = nullptr;
	}

	if (s_instance == this) {
		s_instance = nullptr;
		this->deleteLater();
	} else {
		this->deleteLater();
	}
	blog(LOG_INFO, "[SSPToolbar] shutdownInternal: Finished cleanup");
}
