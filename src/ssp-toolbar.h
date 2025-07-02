#pragma once

#include <QObject>
#include <QToolBar>
#include <QAction>
#include <QMap>
#include <QDockWidget>
#include <QTimer>
#include <atomic>

// Forward declarations
struct QCef;
class QCefWidget;
struct QCefCookieManager;

class SspToolbarManager : public QObject {
	Q_OBJECT
public:
	static SspToolbarManager *instance();
	static void shutdown();
	static SspToolbarManager *checkInstance() { return s_instance; };
	// Interface methods (will emit signals)
	void addSourceAction(const QString &sourceName, const QString &ip);
	void removeSourceAction(const QString &sourceName, const QString &ip);

	// Called from QAction::toggled, should be main-thread safe
	void showBrowserDock(const QString &sourceName, const QString &ip,
			     const QString &sourceKey);

signals:
	void addSourceActionRequested(const QString &sourceName,
				      const QString &ip);
	void removeSourceActionRequested(const QString &sourceName,
					 const QString &ip);

protected:
	// Event filter to catch close events
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void doAddSourceAction(const QString &sourceName, const QString &ip);
	void doRemoveSourceAction(const QString &sourceName, const QString &ip);
	void checkBrowserInitialization(); // Keep this as a slot for QTimer
	void onActiveWindowChanged(QWindow *window);

private:
	explicit SspToolbarManager(QObject *parent = nullptr);
	~SspToolbarManager();

	// Browser initialization methods
	void initializeBrowserPanel();
	// void checkBrowserInitialization(); // Moved to private slots
	void processPendingDocks();
	QCefWidget *createBrowserWidget(QDockWidget *dock,
					const QString &sourceName,
					const QString &ip);
	void suppressUnloadDialog(QCefWidget *widget);
	void scheduleSuppressUnloadDialog(const QString &sourceKey);

	// Toolbar management
	void createToolbar();
	void removeToolbar();
	void shutdownInternal();
	QDockWidget *createBrowserDock(const QString &sourceName,
				       const QString &ip,
				       const QString &sourceKey);

	// Static members
	static SspToolbarManager *s_instance;
	static QCef *m_qcef;
	static QCefCookieManager *panel_cookies;

	// Instance members
	QToolBar *m_toolbar;
	QMainWindow *m_mainWindow;
	QTimer *m_initTimer;
	std::atomic<bool> m_browserInitialized;
	QMap<QString, QAction *> m_sourceActions;
	QMap<QString, QDockWidget *> m_browserDocks;
};