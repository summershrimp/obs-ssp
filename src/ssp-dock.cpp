#include "ssp-dock.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <QThread>
#include <QApplication>
#include <QMainWindow>

//C:\ProgramData\obs-studio\plugins\obs-ssp\bin\64bit why ?
struct SourceFindData {
	QString ip;
	bool found;
	obs_source_t *source;
};

SspDock::SspDock(QWidget *parent) : QDockWidget(parent)
{
	setWindowTitle(obs_module_text("SSPPlugin.Dock.Title"));

	// Set the size to approximately 1/3 to 1/2 of the main window
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	if (mainWindow) {
		QSize mainSize = mainWindow->size();
		int dockWidth =
			mainSize.width() * 0.4; // 40% of main window width
		int dockHeight =
			mainSize.height() * 0.5; // 50% of main window height
		resize(dockWidth, dockHeight);
		setMinimumSize(dockWidth * 0.75, dockHeight * 0.75);
	}

	// Make the dock floating by default and allow it to be docked anywhere
	setFloating(true);
	setAllowedAreas(Qt::AllDockWidgetAreas);

	mainWidget = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(mainWidget);
	layout->setContentsMargins(12, 12, 12,
				   12); // Add margin around the table

	deviceTable = new QTableWidget(mainWidget);
	deviceTable->setColumnCount(3);
	deviceTable->setHorizontalHeaderLabels(
		{obs_module_text("SSPPlugin.Dock.DeviceName"),
		 obs_module_text("SSPPlugin.Dock.IPAddress"),
		 obs_module_text("SSPPlugin.Dock.Action")});

	// Set header style to center-align text
	deviceTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);

	deviceTable->horizontalHeader()->setSectionResizeMode(
		0, QHeaderView::Stretch);
	deviceTable->horizontalHeader()->setSectionResizeMode(
		1, QHeaderView::Stretch);
	deviceTable->horizontalHeader()->setSectionResizeMode(
		2, QHeaderView::Fixed);
	deviceTable->setColumnWidth(2, 120); // Wider column for buttons

	// Set table style
	deviceTable->setSelectionBehavior(QTableWidget::SelectRows);
	deviceTable->setEditTriggers(QTableWidget::NoEditTriggers);
	deviceTable->setSelectionMode(QTableWidget::SingleSelection);
	deviceTable->setAlternatingRowColors(true);
	deviceTable->setShowGrid(true);
	deviceTable->verticalHeader()->setVisible(false);
	deviceTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

	// Adjust row height
	deviceTable->verticalHeader()->setDefaultSectionSize(
		40); // Set taller rows

	layout->addWidget(deviceTable);

	setWidget(mainWidget);

	refreshTimer = new QTimer(this);
	connect(refreshTimer, &QTimer::timeout, this, &SspDock::refreshDevices);
	refreshTimer->start(4000); // Refresh every second

	// Check for existing sources every 5 seconds
	QTimer *sourceCheckTimer = new QTimer(this);
	connect(sourceCheckTimer, &QTimer::timeout, this,
		&SspDock::checkExistingSources);
	sourceCheckTimer->start(20000);

	// Connect signals for thread-safe UI updates
	connect(this, &SspDock::deviceListUpdated, this,
		&SspDock::onDeviceListUpdated, Qt::QueuedConnection);
	connect(this, &SspDock::sourceStateChanged, this,
		&SspDock::onSourceStateChanged, Qt::QueuedConnection);

	// Initial refresh
	QMetaObject::invokeMethod(this, &SspDock::refreshDevices,
				  Qt::QueuedConnection);
}

SspDock::~SspDock()
{
	// Stop the refresh timer
	if (refreshTimer) {
		refreshTimer->stop();
		delete refreshTimer;
		refreshTimer = nullptr;
	}

	// Clear the source buttons map
	sourceButtons.clear();

	// Clear the table
	if (deviceTable) {
		deviceTable->setRowCount(0);
		deviceTable->clear();
	}
}

void SspDock::refreshDevices()
{
	// Emit signal to update UI in the main thread
	if (this->isVisible()) {
		emit deviceListUpdated();
	}
}

void SspDock::onDeviceListUpdated()
{
	if (QThread::currentThread() != qApp->thread()) {
		QMetaObject::invokeMethod(this, &SspDock::onDeviceListUpdated,
					  Qt::QueuedConnection);
		return;
	}

	deviceTable->setRowCount(0);
	sourceButtons.clear();

	SspMDnsIterator iter;
	while (iter.hasNext()) {
		ssp_device_item *item = iter.next();
		if (item == nullptr) {
			continue;
		}

		int row = deviceTable->rowCount();
		deviceTable->insertRow(row);

		QString ip = QString::fromStdString(item->ip_address);
		QString name = QString::fromStdString(item->device_name);

		// Create items with center alignment
		QTableWidgetItem *nameItem = new QTableWidgetItem(name);
		nameItem->setTextAlignment(Qt::AlignCenter);

		QTableWidgetItem *ipItem = new QTableWidgetItem(ip);
		ipItem->setTextAlignment(Qt::AlignCenter);

		deviceTable->setItem(row, 0, nameItem);
		deviceTable->setItem(row, 1, ipItem);

		// Create the button with better styling
		QPushButton *button = new QPushButton(this);
		button->setMinimumWidth(100);
		button->setFixedHeight(30);
		button->setStyleSheet(
			"margin: 4px 8px;"); // Add margins around the button
		sourceButtons[ip] = button;
		updateSourceButton(ip, isDeviceAddedAsSource(ip));
		connect(button, &QPushButton::clicked, this,
			&SspDock::handleSourceButton);

		// Create widget container with padding for the button
		QWidget *buttonWidget = new QWidget();
		QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWidget);
		buttonLayout->addWidget(button);
		buttonLayout->setAlignment(Qt::AlignCenter);
		buttonLayout->setContentsMargins(
			4, 6, 4, 6); // Add padding around the button
		deviceTable->setCellWidget(row, 2, buttonWidget);
	}
}

void SspDock::handleSourceButton()
{
	if (QThread::currentThread() != qApp->thread()) {
		QMetaObject::invokeMethod(this, &SspDock::handleSourceButton,
					  Qt::QueuedConnection);
		return;
	}

	QPushButton *button = qobject_cast<QPushButton *>(sender());
	if (!button)
		return;

	// Find the IP address for this button
	QString ip;
	for (auto it = sourceButtons.begin(); it != sourceButtons.end(); ++it) {
		if (it.value() == button) {
			ip = it.key();
			break;
		}
	}
	if (ip.isEmpty())
		return;

	// Double-check if the button is disabled (should be prevented by Qt, but just to be safe)
	if (!button->isEnabled()) {
		return;
	}

	// Find the device name
	QString name;
	for (int row = 0; row < deviceTable->rowCount(); row++) {
		if (deviceTable->item(row, 1)->text() == ip) {
			name = deviceTable->item(row, 0)->text();
			break;
		}
	}

	if (isDeviceAddedAsSource(ip)) {
		removeSource(ip);
	} else {
		addSource(ip, name);
	}
}

void SspDock::checkExistingSources()
{
	if (QThread::currentThread() != qApp->thread()) {
		QMetaObject::invokeMethod(this, &SspDock::checkExistingSources,
					  Qt::QueuedConnection);
		return;
	}

	for (auto it = sourceButtons.begin(); it != sourceButtons.end(); ++it) {
		emit sourceStateChanged(it.key(),
					isDeviceAddedAsSource(it.key()));
	}
}

void SspDock::onSourceStateChanged(const QString &ip, bool isSource)
{
	if (QThread::currentThread() != qApp->thread()) {
		QMetaObject::invokeMethod(
			this,
			[this, ip, isSource]() {
				onSourceStateChanged(ip, isSource);
			},
			Qt::QueuedConnection);
		return;
	}

	updateSourceButton(ip, isSource);
}

void SspDock::updateSourceButton(const QString &ip, bool isSource)
{
	if (QThread::currentThread() != qApp->thread()) {
		QMetaObject::invokeMethod(
			this,
			[this, ip, isSource]() {
				updateSourceButton(ip, isSource);
			},
			Qt::QueuedConnection);
		return;
	}

	QPushButton *button = sourceButtons.value(ip);
	if (!button)
		return;

	// Set the button text based on source status
	if (isSource) {
		//button->setText(obs_module_text("SSPPlugin.Dock.RemoveSource"));
		button->setEnabled(false);
		button->setHidden(true);
	} else {
		button->setText(obs_module_text("SSPPlugin.Dock.AddSource"));
		button->setEnabled(true);
		button->setHidden(false);
	}
}

bool SspDock::isDeviceAddedAsSource(const QString &ip)
{
	SourceFindData data = {ip, false, nullptr};

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			SourceFindData *data =
				static_cast<SourceFindData *>(param);

			if (strcmp(obs_source_get_id(source), "ssp_source") ==
			    0) {
				obs_data_t *settings =
					obs_source_get_settings(source);
				const char *source_ip = obs_data_get_string(
					settings, "ssp_source_ip");
				if (QString(source_ip) == data->ip) {
					data->found = true;
					obs_data_release(settings);
					return false;
				}
				obs_data_release(settings);
			}
			return true;
		},
		&data);

	return data.found;
}

struct AddSourceData {
	/* Input data */
	obs_source_t *source;
	bool visible;
	obs_sceneitem_t *scene_item;
};
static void AddSource(void *_data, obs_scene_t *scene)
{
	AddSourceData *data = (AddSourceData *)_data;
	data->scene_item = obs_scene_add(scene, data->source);
	obs_sceneitem_set_visible(data->scene_item, data->visible);
}

obs_sceneitem_t *CreateSceneItem(obs_source_t *source, obs_scene_t *scene,
				 bool sceneItemEnabled)
{
	// Sanity check for valid scene
	if (!(source && scene))
		return nullptr;

	// Create data struct and populate for scene item creation
	AddSourceData data;
	data.source = source;
	data.visible = true;

	// Enter graphics context and create the scene item
	obs_enter_graphics();
	obs_scene_atomic_update(scene, AddSource, &data);
	obs_leave_graphics();

	obs_sceneitem_addref(data.scene_item);

	return data.scene_item;
}

obs_sceneitem_t *CreateInput(std::string inputName, std::string inputKind,
			     obs_data_t *inputSettings, obs_scene_t *scene,
			     bool sceneItemEnabled)
{
	// Create the input
	OBSSourceAutoRelease input = obs_source_create(
		inputKind.c_str(), inputName.c_str(), inputSettings, nullptr);

	// Check that everything was created properly
	if (!input)
		return nullptr;

	// Apparently not all default input properties actually get applied on creation (smh)
	uint32_t flags = obs_source_get_output_flags(input);
	if ((flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0)
		obs_source_set_monitoring_type(
			input, OBS_MONITORING_TYPE_MONITOR_ONLY);

	// Create a scene item for the input
	obs_sceneitem_t *ret = CreateSceneItem(input, scene, sceneItemEnabled);

	// If creation failed, remove the input
	if (!ret)
		obs_source_remove(input);

	return ret;
}

void SspDock::addSource(const QString &ip, const QString &name)
{
	// Create settings for the source
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "ssp_source_ip", ip.toUtf8().constData());

	//obs_data_set_string(settings, "ssp_source_ip", ip.toUtf8().constData());
	//obs_data_set_string(settings, "ssp_source_ip", ip.toUtf8().constData());

	//OBSDataAutoRelease inputSettings = settings;

	// Enter graphics context to create and add the source

	OBSSourceAutoRelease current_scene_source =
		obs_frontend_get_current_scene();
	if (current_scene_source == nullptr) {
		return;
	}
	OBSScene scene = obs_scene_from_source(current_scene_source);

	OBSSceneItemAutoRelease sceneItem = CreateInput(
		name.toUtf8().constData(), "ssp_source", settings, scene, true);

	emit sourceStateChanged(ip, true);
}

void SspDock::removeSource(const QString &ip)
{
	SourceFindData data = {ip, false, nullptr};

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			SourceFindData *data =
				static_cast<SourceFindData *>(param);

			if (strcmp(obs_source_get_id(source), "ssp_source") ==
			    0) {
				obs_data_t *settings =
					obs_source_get_settings(source);
				const char *source_ip = obs_data_get_string(
					settings, "ssp_source_ip");
				if (QString(source_ip) == data->ip) {
					data->source = source;
					obs_data_release(settings);
					return false;
				}
				obs_data_release(settings);
			}
			return true;
		},
		&data);

	if (data.source) {

		obs_source_remove(data.source);
		obs_source_release(data.source);
	}

	emit sourceStateChanged(ip, false);
}