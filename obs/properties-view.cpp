#include <QFormLayout>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include "qt-wrappers.hpp"
#include "properties-view.hpp"
#include <string>

using namespace std;

void OBSPropertiesView::RefreshProperties()
{
	children.clear();
	if (widget)
		widget->deleteLater();

	widget = new QWidget();

	QFormLayout *layout = new QFormLayout;
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	widget->setLayout(layout);

	QSizePolicy mainPolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	//widget->setSizePolicy(policy);
	layout->setSizeConstraint(QLayout::SetMaximumSize);
	layout->setLabelAlignment(Qt::AlignRight);

	obs_property_t property = obs_properties_first(properties);

	while (property) {
		AddProperty(property, layout);
		obs_property_next(&property);
	}

	setWidgetResizable(true);
	setWidget(widget);
	setSizePolicy(mainPolicy);

	lastFocused.clear();
	if (lastWidget) {
		lastWidget->setFocus(Qt::OtherFocusReason);
		lastWidget = nullptr;
	}
}

OBSPropertiesView::OBSPropertiesView(OBSData settings_,
		obs_properties_t properties_, void *obj_,
		PropertiesUpdateCallback callback_, int minSize_)
	: QScrollArea (nullptr),
	  widget      (nullptr),
	  properties  (properties_),
	  settings    (settings_),
	  obj         (obj_),
	  callback    (callback_),
	  minSize     (minSize_),
	  lastWidget  (nullptr)
{
	setFrameShape(QFrame::NoFrame);
	RefreshProperties();
}

QWidget *OBSPropertiesView::NewWidget(obs_property_t prop, QWidget *widget,
		const char *signal)
{
	WidgetInfo *info = new WidgetInfo(this, prop, widget);
	connect(widget, signal, info, SLOT(ControlChanged()));
	children.push_back(std::move(unique_ptr<WidgetInfo>(info)));
	return widget;
}

QWidget *OBSPropertiesView::AddCheckbox(obs_property_t prop)
{
	const char *name = obs_property_name(prop);
	const char *desc = obs_property_description(prop);
	bool       val   = obs_data_getbool(settings, name);

	QCheckBox *checkbox = new QCheckBox(QT_UTF8(desc));
	checkbox->setCheckState(val ? Qt::Checked : Qt::Unchecked);
	return NewWidget(prop, checkbox, SIGNAL(stateChanged(int)));
}

QWidget *OBSPropertiesView::AddText(obs_property_t prop)
{
	const char    *name = obs_property_name(prop);
	const char    *val  = obs_data_getstring(settings, name);
	obs_text_type type  = obs_proprety_text_type(prop);  
	QLineEdit     *edit = new QLineEdit();

	if (type == OBS_TEXT_PASSWORD)
		edit->setEchoMode(QLineEdit::Password);

	edit->setText(QT_UTF8(val));

	return NewWidget(prop, edit, SIGNAL(textEdited(const QString &)));
}

QWidget *OBSPropertiesView::AddPath(obs_property_t prop, QFormLayout *layout)
{
	/* TODO */
	UNUSED_PARAMETER(prop);
	UNUSED_PARAMETER(layout);
	return nullptr;
}

QWidget *OBSPropertiesView::AddInt(obs_property_t prop)
{
	const char *name = obs_property_name(prop);
	int        val   = (int)obs_data_getint(settings, name);
	QSpinBox   *spin = new QSpinBox();

	spin->setMinimum(obs_property_int_min(prop));
	spin->setMaximum(obs_property_int_max(prop));
	spin->setSingleStep(obs_property_int_step(prop));
	spin->setValue(val);

	return NewWidget(prop, spin, SIGNAL(valueChanged(int)));
}

QWidget *OBSPropertiesView::AddFloat(obs_property_t prop)
{
	const char     *name = obs_property_name(prop);
	double         val   = obs_data_getdouble(settings, name);
	QDoubleSpinBox *spin = new QDoubleSpinBox();

	spin->setMinimum(obs_property_float_min(prop));
	spin->setMaximum(obs_property_float_max(prop));
	spin->setSingleStep(obs_property_float_step(prop));
	spin->setValue(val);

	return NewWidget(prop, spin, SIGNAL(valueChanged(double)));
}

static void AddComboItem(QComboBox *combo, obs_property_t prop,
		obs_combo_format format, size_t idx)
{
	const char *name = obs_property_list_item_name(prop, idx);
	QVariant var;

	if (format == OBS_COMBO_FORMAT_INT) {
		long long val = obs_property_list_item_int(prop, idx);
		var = QVariant::fromValue<long long>(val);

	} else if (format == OBS_COMBO_FORMAT_FLOAT) {
		double val = obs_property_list_item_float(prop, idx);
		var = QVariant::fromValue<double>(val);

	} else if (format == OBS_COMBO_FORMAT_STRING) {
		var = obs_property_list_item_string(prop, idx);
	}

	combo->addItem(QT_UTF8(name), var);
}

QWidget *OBSPropertiesView::AddList(obs_property_t prop)
{
	const char       *name  = obs_property_name(prop);
	QComboBox        *combo = new QComboBox();
	obs_combo_type   type   = obs_property_list_type(prop);
	obs_combo_format format = obs_property_list_format(prop);
	size_t           count  = obs_property_list_item_count(prop);
	int              idx    = -1;

	for (size_t i = 0; i < count; i++)
		AddComboItem(combo, prop, format, i);

	if (format == OBS_COMBO_FORMAT_INT) {
		int    val       = (int)obs_data_getint(settings, name);
		string valString = to_string(val);
		idx              = combo->findData(QT_UTF8(valString.c_str()));

	} else if (format == OBS_COMBO_FORMAT_FLOAT) {
		double val       = obs_data_getdouble(settings, name);
		string valString = to_string(val);
		idx              = combo->findData(QT_UTF8(valString.c_str()));

	} else if (format == OBS_COMBO_FORMAT_STRING) {
		const char *val  = obs_data_getstring(settings, name);

		if (type == OBS_COMBO_TYPE_EDITABLE)
			combo->lineEdit()->setText(val);
		else
			idx      = combo->findData(QT_UTF8(val));
	}

	if (type == OBS_COMBO_TYPE_EDITABLE) {
		combo->setEditable(true);
		return NewWidget(prop, combo,
				SLOT(editTextChanged(const QString &)));
	}

	if (idx != -1)
		combo->setCurrentIndex(idx);

	WidgetInfo *info = new WidgetInfo(this, prop, combo);
	connect(combo, SIGNAL(currentIndexChanged(int)), info,
				SLOT(ControlChanged()));
	children.push_back(std::move(unique_ptr<WidgetInfo>(info)));

	/* trigger a settings update if the index was not found */
	if (idx == -1)
		info->ControlChanged();

	return combo;
}

void OBSPropertiesView::AddProperty(obs_property_t property,
		QFormLayout *layout)
{
	const char        *name = obs_property_name(property);
	obs_property_type type  = obs_property_get_type(property);

	if (!obs_property_visible(property))
		return;

	QWidget *widget = nullptr;

	switch (type) {
	case OBS_PROPERTY_INVALID:
		return;
	case OBS_PROPERTY_BOOL:
		widget = AddCheckbox(property);
		break;
	case OBS_PROPERTY_INT:
		widget = AddInt(property);
		break;
	case OBS_PROPERTY_FLOAT:
		widget = AddFloat(property);
		break;
	case OBS_PROPERTY_TEXT:
		widget = AddText(property);
		break;
	case OBS_PROPERTY_PATH:
		AddPath(property, layout);
		break;
	case OBS_PROPERTY_LIST:
		widget = AddList(property);
		break;
	case OBS_PROPERTY_COLOR:
		/* TODO */
		break;
	}

	if (!widget)
		return;

	if (!obs_property_enabled(property))
		widget->setEnabled(false);

	QLabel *label = nullptr;
	if (type != OBS_PROPERTY_BOOL)
		label = new QLabel(QT_UTF8(obs_property_description(property)));

	if (label && minSize) {
		label->setMinimumWidth(minSize);
		label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	}

	layout->addRow(label, widget);

	if (!lastFocused.empty())
		if (lastFocused.compare(name) == 0)
			lastWidget = widget;
}

void WidgetInfo::BoolChanged(const char *setting)
{
	QCheckBox *checkbox = static_cast<QCheckBox*>(widget);
	obs_data_setbool(view->settings, setting,
			checkbox->checkState() == Qt::Checked);
}

void WidgetInfo::IntChanged(const char *setting)
{
	QSpinBox *spin = static_cast<QSpinBox*>(widget);
	obs_data_setint(view->settings, setting, spin->value());
}

void WidgetInfo::FloatChanged(const char *setting)
{
	QDoubleSpinBox *spin = static_cast<QDoubleSpinBox*>(widget);
	obs_data_setdouble(view->settings, setting, spin->value());
}

void WidgetInfo::TextChanged(const char *setting)
{
	QLineEdit *edit = static_cast<QLineEdit*>(widget);
	obs_data_setstring(view->settings, setting, QT_TO_UTF8(edit->text()));
}

void WidgetInfo::PathChanged(const char *setting)
{
	/* TODO */
	UNUSED_PARAMETER(setting);
}

void WidgetInfo::ListChanged(const char *setting)
{
	QComboBox        *combo = static_cast<QComboBox*>(widget);
	obs_combo_format format = obs_property_list_format(property);
	obs_combo_type   type   = obs_property_list_type(property);
	QVariant         data;

	if (type == OBS_COMBO_TYPE_EDITABLE) {
		data = combo->currentText();
	} else {
		int index = combo->currentIndex();
		if (index != -1)
			data = combo->itemData(index);
		else
			return;
	}

	switch (format) {
	case OBS_COMBO_FORMAT_INVALID:
		return;
	case OBS_COMBO_FORMAT_INT:
		obs_data_setint(view->settings, setting,
				data.value<long long>());
		break;
	case OBS_COMBO_FORMAT_FLOAT:
		obs_data_setdouble(view->settings, setting,
				data.value<double>());
		break;
	case OBS_COMBO_FORMAT_STRING:
		obs_data_setstring(view->settings, setting,
				QT_TO_UTF8(data.toString()));
		break;
	}
}

void WidgetInfo::ColorChanged(const char *setting)
{
	/* TODO */
	UNUSED_PARAMETER(setting);
}

void WidgetInfo::ControlChanged()
{
	const char        *setting = obs_property_name(property);
	obs_property_type type     = obs_property_get_type(property);

	switch (type) {
	case OBS_PROPERTY_INVALID: return;
	case OBS_PROPERTY_BOOL:    BoolChanged(setting); break;
	case OBS_PROPERTY_INT:     IntChanged(setting); break;
	case OBS_PROPERTY_FLOAT:   FloatChanged(setting); break;
	case OBS_PROPERTY_TEXT:    TextChanged(setting); break;
	case OBS_PROPERTY_PATH:    PathChanged(setting); break;
	case OBS_PROPERTY_LIST:    ListChanged(setting); break;
	case OBS_PROPERTY_COLOR:   ColorChanged(setting); break;
	}

	view->callback(view->obj, view->settings);
	if (obs_property_modified(property, view->settings)) {
		view->lastFocused = setting;
		QMetaObject::invokeMethod(view, "RefreshProperties",
				Qt::QueuedConnection);
	}
}
