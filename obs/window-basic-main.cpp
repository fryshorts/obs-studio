/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>
    Copyright (C) 2014 by Zachary Lund <admin@computerquip.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <obs.hpp>
#include <QMessageBox>
#include <QShowEvent>
#include <QFileDialog>

#include "obs-app.hpp"
#include "window-basic-settings.hpp"
#include "window-namedialog.hpp"
#include "window-basic-main.hpp"
#include "qt-wrappers.hpp"

#include "ui_OBSBasic.h"

using namespace std;

Q_DECLARE_METATYPE(OBSScene);
Q_DECLARE_METATYPE(OBSSceneItem);

OBSBasic::OBSBasic(QWidget *parent)
	: OBSMainWindow (parent),
	  ui            (new Ui::OBSBasic),
	  outputTest    (NULL)
{
	ui->setupUi(this);
}

void OBSBasic::OBSInit()
{
	/* make sure it's fully displayed before doing any initialization */
	show();
	App()->processEvents();

	if (!obs_startup())
		throw "Failed to initialize libobs";
	if (!InitGraphics())
		throw "Failed to initialize graphics";
	if (!InitAudio())
		throw "Failed to initialize audio";

	signal_handler_connect(obs_signalhandler(), "source-add",
			OBSBasic::SourceAdded, this);
	signal_handler_connect(obs_signalhandler(), "source-remove",
			OBSBasic::SourceRemoved, this);
	signal_handler_connect(obs_signalhandler(), "channel-change",
			OBSBasic::ChannelChanged, this);

	/* TODO: this is a test */
	obs_load_module("test-input");
	obs_load_module("obs-ffmpeg");

	/* HACK: fixes a qt bug with native widgets with native repaint */
	ui->previewContainer->repaint();
}

OBSBasic::~OBSBasic()
{
	/* free the lists before shutting down to remove the scene/item
	 * references */
	ui->sources->clear();
	ui->scenes->clear();
	obs_shutdown();
}

OBSScene OBSBasic::GetCurrentScene()
{
	QListWidgetItem *item = ui->scenes->currentItem();
	return item ? item->data(Qt::UserRole).value<OBSScene>() : nullptr;
}

OBSSceneItem OBSBasic::GetCurrentSceneItem()
{
	QListWidgetItem *item = ui->sources->currentItem();
	return item ? item->data(Qt::UserRole).value<OBSSceneItem>() : nullptr;
}

void OBSBasic::UpdateSources(OBSScene scene)
{
	ui->sources->clear();

	obs_scene_enum_items(scene,
			[] (obs_scene_t scene, obs_sceneitem_t item, void *p)
			{
				OBSBasic *window = static_cast<OBSBasic*>(p);
				window->AddSceneItem(item);
				return true;
			}, this);
}

/* Qt callbacks for invokeMethod */

void OBSBasic::AddScene(OBSSource source)
{
	const char *name  = obs_source_getname(source);
	obs_scene_t scene = obs_scene_fromsource(source);

	QListWidgetItem *item = new QListWidgetItem(QT_UTF8(name));
	item->setData(Qt::UserRole, QVariant::fromValue(OBSScene(scene)));
	ui->scenes->addItem(item);

	signal_handler_t handler = obs_source_signalhandler(source);
	signal_handler_connect(handler, "add", OBSBasic::SceneItemAdded, this);
	signal_handler_connect(handler, "remove", OBSBasic::SceneItemRemoved,
			this);
}

void OBSBasic::RemoveScene(OBSSource source)
{
	const char *name = obs_source_getname(source);

	QListWidgetItem *sel = ui->scenes->currentItem();
	QList<QListWidgetItem*> items = ui->scenes->findItems(QT_UTF8(name),
			Qt::MatchExactly);

	if (sel != nullptr) {
		if (items.contains(sel))
			ui->sources->clear();
		delete sel;
	}
}

void OBSBasic::AddSceneItem(OBSSceneItem item)
{
	obs_scene_t  scene  = obs_sceneitem_getscene(item);
	obs_source_t source = obs_sceneitem_getsource(item);
	const char   *name  = obs_source_getname(source);

	if (GetCurrentScene() == scene) {
		QListWidgetItem *listItem = new QListWidgetItem(QT_UTF8(name));
		listItem->setData(Qt::UserRole,
				QVariant::fromValue(OBSSceneItem(item)));

		ui->sources->insertItem(0, listItem);
	}

	sourceSceneRefs[source] = sourceSceneRefs[source] + 1;
}

void OBSBasic::RemoveSceneItem(OBSSceneItem item)
{
	obs_scene_t scene = obs_sceneitem_getscene(item);

	if (GetCurrentScene() == scene) {
		for (int i = 0; i < ui->sources->count(); i++) {
			QListWidgetItem *listItem = ui->sources->item(i);
			QVariant userData = listItem->data(Qt::UserRole);

			if (userData.value<OBSSceneItem>() == item) {
				delete listItem;
				break;
			}
		}
	}

	obs_source_t source = obs_sceneitem_getsource(item);

	int scenes = sourceSceneRefs[source] - 1;
	if (scenes == 0) {
		obs_source_remove(source);
		sourceSceneRefs.erase(source);
	}
}

void OBSBasic::UpdateSceneSelection(OBSSource source)
{
	if (source) {
		obs_source_type type;
		obs_source_gettype(source, &type, NULL);

		if (type != SOURCE_SCENE)
			return;

		obs_scene_t scene = obs_scene_fromsource(source);
		const char *name = obs_source_getname(source);

		QListWidgetItem *sel = ui->scenes->currentItem();
		QList<QListWidgetItem*> items =
			ui->scenes->findItems(QT_UTF8(name), Qt::MatchExactly);

		if (items.contains(sel)) {
			ui->scenes->setCurrentItem(sel);
			UpdateSources(scene);
		}
	}
}

/* OBS Callbacks */

void OBSBasic::SceneItemAdded(void *data, calldata_t params)
{
	OBSBasic *window = static_cast<OBSBasic*>(data);

	obs_scene_t scene = (obs_scene_t)calldata_ptr(params, "scene");
	obs_sceneitem_t item = (obs_sceneitem_t)calldata_ptr(params, "item");

	QMetaObject::invokeMethod(window, "AddSceneItem",
			Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void OBSBasic::SceneItemRemoved(void *data, calldata_t params)
{
	OBSBasic *window = static_cast<OBSBasic*>(data);

	obs_scene_t scene = (obs_scene_t)calldata_ptr(params, "scene");
	obs_sceneitem_t item = (obs_sceneitem_t)calldata_ptr(params, "item");

	QMetaObject::invokeMethod(window, "RemoveSceneItem",
			Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void OBSBasic::SourceAdded(void *data, calldata_t params)
{
	obs_source_t source = (obs_source_t)calldata_ptr(params, "source");

	obs_source_type type;
	obs_source_gettype(source, &type, NULL);

	if (type == SOURCE_SCENE)
		QMetaObject::invokeMethod(static_cast<OBSBasic*>(data),
				"AddScene",
				Q_ARG(OBSSource, OBSSource(source)));
}

void OBSBasic::SourceRemoved(void *data, calldata_t params)
{
	obs_source_t source = (obs_source_t)calldata_ptr(params, "source");

	obs_source_type type;
	obs_source_gettype(source, &type, NULL);

	if (type == SOURCE_SCENE)
		QMetaObject::invokeMethod(static_cast<OBSBasic*>(data),
				"RemoveScene",
				Q_ARG(OBSSource, OBSSource(source)));
}

void OBSBasic::ChannelChanged(void *data, calldata_t params)
{
	obs_source_t source = (obs_source_t)calldata_ptr(params, "source");
	uint32_t channel = calldata_uint32(params, "channel");

	if (channel == 0)
		QMetaObject::invokeMethod(static_cast<OBSBasic*>(data),
				"UpdateSceneSelection",
				Q_ARG(OBSSource, OBSSource(source)));
}

/* Main class functions */

bool OBSBasic::InitGraphics()
{
	struct obs_video_info ovi;

	App()->GetConfigFPS(ovi.fps_num, ovi.fps_den);

	ovi.graphics_module = App()->GetRenderModule();
	ovi.base_width    = (uint32_t)config_get_uint(GetGlobalConfig(),
			"Video", "BaseCX");
	ovi.base_height   = (uint32_t)config_get_uint(GetGlobalConfig(),
			"Video", "BaseCY");
	ovi.output_width  = (uint32_t)config_get_uint(GetGlobalConfig(),
			"Video", "OutputCX");
	ovi.output_height = (uint32_t)config_get_uint(GetGlobalConfig(),
			"Video", "OutputCY");
	ovi.output_format = VIDEO_FORMAT_I420;
	ovi.adapter       = 0;

	QTToGSWindow(ui->preview, ovi.window);

	//required to make opengl display stuff on osx(?)
	ResizePreview(ovi.base_width, ovi.base_height);

	QSize size = ui->preview->size();
	ovi.window_width  = size.width();
	ovi.window_height = size.height();

	return obs_reset_video(&ovi);
}

bool OBSBasic::InitAudio()
{
	/* TODO: load audio settings from config */
	struct audio_output_info ai;
	ai.name = "test";
	ai.samples_per_sec = 44100;
	ai.format = AUDIO_FORMAT_FLOAT_PLANAR;
	ai.speakers = SPEAKERS_STEREO;
	ai.buffer_ms = 700;

	return obs_reset_audio(&ai);
}

void OBSBasic::ResizePreview(uint32_t cx, uint32_t cy)
{
	double targetAspect, baseAspect;
	QSize  targetSize;
	int x, y;

	/* resize preview panel to fix to the top section of the window */
	targetSize   = ui->previewContainer->size();
	targetAspect = double(targetSize.width()) / double(targetSize.height());
	baseAspect   = double(cx) / double(cy);

	if (targetAspect > baseAspect) {
		cx = targetSize.height() * baseAspect;
		cy = targetSize.height();
	} else {
		cx = targetSize.width();
		cy = targetSize.width() / baseAspect;
	}

	x = targetSize.width() /2 - cx/2;
	y = targetSize.height()/2 - cy/2;

	ui->preview->setGeometry(x, y, cx, cy);

	graphics_t graphics = obs_graphics();
	if (graphics && isVisible()) {
		gs_entercontext(graphics);
		gs_resize(cx, cy);
		gs_leavecontext();
	}
}

void OBSBasic::closeEvent(QCloseEvent *event)
{
}

void OBSBasic::changeEvent(QEvent *event)
{
}

void OBSBasic::resizeEvent(QResizeEvent *event)
{
	struct obs_video_info ovi;

	if (obs_get_video_info(&ovi))
		ResizePreview(ovi.base_width, ovi.base_height);
}

void OBSBasic::on_action_New_triggered()
{
}

void OBSBasic::on_action_Open_triggered()
{
}

void OBSBasic::on_action_Save_triggered()
{
}

void OBSBasic::on_scenes_itemChanged(QListWidgetItem *item)
{
	obs_source_t source = NULL;

	if (item) {
		obs_scene_t scene;

		scene = item->data(Qt::UserRole).value<OBSScene>();
		source = obs_scene_getsource(scene);
		UpdateSources(scene);
	}

	/* TODO: allow transitions */
	obs_set_output_source(0, source);
}

void OBSBasic::on_scenes_customContextMenuRequested(const QPoint &pos)
{
}

void OBSBasic::on_actionAddScene_triggered()
{
	string name;
	bool accepted = NameDialog::AskForName(this,
			QTStr("MainWindow.AddSceneDlg.Title"),
			QTStr("MainWindow.AddSceneDlg.Text"),
			name);

	if (accepted) {
		obs_source_t source = obs_get_source_by_name(name.c_str());
		if (source) {
			QMessageBox::information(this,
					QTStr("MainWindow.NameExists.Title"),
					QTStr("MainWindow.NameExists.Text"));

			obs_source_release(source);
			on_actionAddScene_triggered();
			return;
		}

		obs_scene_t scene = obs_scene_create(name.c_str());
		source = obs_scene_getsource(scene);
		obs_add_source(source);
		obs_scene_release(scene);

		obs_set_output_source(0, source);
	}
}

void OBSBasic::on_actionRemoveScene_triggered()
{
	QListWidgetItem *item = ui->scenes->currentItem();
	if (!item)
		return;

	QVariant userData = item->data(Qt::UserRole);
	obs_scene_t scene = userData.value<OBSScene>();
	obs_source_t source = obs_scene_getsource(scene);
	obs_source_remove(source);
}

void OBSBasic::on_actionSceneProperties_triggered()
{
}

void OBSBasic::on_actionSceneUp_triggered()
{
}

void OBSBasic::on_actionSceneDown_triggered()
{
}

void OBSBasic::on_sources_itemChanged(QListWidgetItem *item)
{
}

void OBSBasic::on_sources_customContextMenuRequested(const QPoint &pos)
{
}

void OBSBasic::AddSource(obs_scene_t scene, const char *id)
{
	string name;

	bool success = false;
	while (!success) {
		bool accepted = NameDialog::AskForName(this,
				Str("MainWindow.AddSourceDlg.Title"),
				Str("MainWindow.AddSourceDlg.Text"),
				name);

		if (!accepted)
			break;

		obs_source_t source = obs_get_source_by_name(name.c_str());
		if (!source) {
			success = true;
		} else {
			QMessageBox::information(this,
					QTStr("MainWindow.NameExists.Title"),
					QTStr("MainWindow.NameExists.Text"));
			obs_source_release(source);
		}
	}

	if (success) {
		obs_source_t source = obs_source_create(SOURCE_INPUT, id,
				name.c_str(), NULL);

		sourceSceneRefs[source] = 0;

		obs_add_source(source);
		obs_sceneitem_t item = obs_scene_add(scene, source);
		obs_source_release(source);
	}
}

void OBSBasic::AddSourcePopupMenu(const QPoint &pos)
{
	OBSScene scene = GetCurrentScene();
	const char *type;
	bool foundValues = false;
	size_t idx = 0;

	if (!scene)
		return;

	QMenu popup;
	while (obs_enum_input_types(idx++, &type)) {
		const char *name = obs_source_getdisplayname(SOURCE_INPUT,
				type, App()->GetLocale());

		QAction *popupItem = new QAction(QT_UTF8(name), this);
		popupItem->setData(QT_UTF8(type));
		popup.addAction(popupItem);

		foundValues = true;
	}

	if (foundValues) {
		QAction *ret = popup.exec(pos);
		if (ret)
			AddSource(scene, ret->data().toString().toUtf8());
	}
}

void OBSBasic::on_actionAddSource_triggered()
{
	AddSourcePopupMenu(QCursor::pos());
}

void OBSBasic::on_actionRemoveSource_triggered()
{
	OBSSceneItem item = GetCurrentSceneItem();
	if (item)
		obs_sceneitem_remove(item);
}

void OBSBasic::on_actionSourceProperties_triggered()
{
}

void OBSBasic::on_actionSourceUp_triggered()
{
}

void OBSBasic::on_actionSourceDown_triggered()
{
}

void OBSBasic::on_recordButton_clicked()
{
	if (outputTest) {
		obs_output_destroy(outputTest);
		outputTest = NULL;
		ui->recordButton->setText("Start Recording");
	} else {
		QString path = QFileDialog::getSaveFileName(this,
				"Please enter a file name", QString(),
				"Video Files (*.avi)");

		if (path.isNull() || path.isEmpty())
			return;

		obs_data_t data = obs_data_create();
		obs_data_setstring(data, "filename", QT_TO_UTF8(path));

		outputTest = obs_output_create("ffmpeg_output", "test", data);
		obs_data_release(data);

		if (!obs_output_start(outputTest)) {
			obs_output_destroy(outputTest);
			outputTest = NULL;
			return;
		}

		ui->recordButton->setText("Stop Recording");
	}
}

void OBSBasic::on_settingsButton_clicked()
{
	OBSBasicSettings settings(this);
	settings.exec();
}
