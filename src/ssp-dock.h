#pragma once

#include <QDockWidget>
#include <QTableWidget>
#include <QTimer>
#include <QPushButton>
#include <QMap>
#include "ssp-mdns.h"

class SspDock : public QDockWidget {
	Q_OBJECT
public:
	explicit SspDock(QWidget *parent = nullptr);
	~SspDock();

private slots:
	void refreshDevices();
	void handleSourceButton();
	void checkExistingSources();
	void updateSourceButton(const QString &ip, bool isSource);
	void onDeviceListUpdated();
	void onSourceStateChanged(const QString &ip, bool isSource);

private:
	QWidget *mainWidget;
	QTableWidget *deviceTable;
	QTimer *refreshTimer;
	QMap<QString, QPushButton *> sourceButtons;

	bool isDeviceAddedAsSource(const QString &ip);
	void addSource(const QString &ip, const QString &name);
	void removeSource(const QString &ip);

signals:
	void deviceListUpdated();
	void sourceStateChanged(const QString &ip, bool isSource);
};