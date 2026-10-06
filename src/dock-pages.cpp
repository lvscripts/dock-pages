#include "dock-pages.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QAction>
#include <QDockWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

// Eigene Versionsnummer für saveState/restoreState
static constexpr int STATE_VERSION = 0x4450;

static QString T(const char *s)
{
	return QString::fromUtf8(s);
}

DockPages::DockPages(QMainWindow *mainWindow) : QObject(mainWindow), mw(mainWindow)
{
	registerHotkeys();
	load();
	buildToolbar();
}

/* ------------------------------------------------------------------ */
/* Oberfläche                                                          */
/* ------------------------------------------------------------------ */

void DockPages::buildToolbar()
{
	toolbar = new QToolBar(T("Dock-Seiten"), mw);
	toolbar->setObjectName(QStringLiteral("DockPagesToolbar"));
	toolbar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
	toolbar->setFloatable(false);

	tabs = new QTabBar(toolbar);
	tabs->setExpanding(false);
	tabs->setMovable(true);
	tabs->setDocumentMode(true);
	tabs->setUsesScrollButtons(true);
	tabs->setContextMenuPolicy(Qt::CustomContextMenu);
	tabs->setToolTip(T("Doppelklick: umbenennen · Rechtsklick: weitere Optionen"));
	toolbar->addWidget(tabs);

	auto *addBtn = new QToolButton(toolbar);
	addBtn->setText(QStringLiteral("+"));
	addBtn->setToolTip(T("Neue leere Seite"));
	toolbar->addWidget(addBtn);

	toolbar->addSeparator();

	previewBtn = new QToolButton(toolbar);
	previewBtn->setText(T("Vorschau"));
	previewBtn->setCheckable(true);
	previewBtn->setChecked(true);
	previewBtn->setToolTip(T("Vorschau auf dieser Seite ein- oder ausblenden"));
	toolbar->addWidget(previewBtn);

	connect(addBtn, &QToolButton::clicked, this, [this] { addEmptyPage(); });
	connect(previewBtn, &QToolButton::toggled, this, [this](bool visible) { setPreviewHidden(!visible); });
	connect(tabs, &QTabBar::currentChanged, this, [this](int idx) {
		if (!updatingTabs)
			switchTo(idx);
	});
	connect(tabs, &QTabBar::tabMoved, this, [this](int from, int to) {
		pages.move(from, to);
		onTabMoved();
	});
	connect(tabs, &QTabBar::tabBarDoubleClicked, this, [this](int idx) {
		if (idx >= 0)
			renamePage(idx);
	});
	connect(tabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint &p) { showContextMenu(p); });

	mw->addToolBar(Qt::TopToolBarArea, toolbar);
	rebuildTabs();
}

void DockPages::rebuildTabs()
{
	updatingTabs = true;
	while (tabs->count() > 0)
		tabs->removeTab(0);
	for (const auto &p : pages)
		tabs->addTab(p.name);
	if (!pages.isEmpty())
		tabs->setCurrentIndex(current);
	updatingTabs = false;
}

void DockPages::showContextMenu(const QPoint &pos)
{
	const int idx = tabs->tabAt(pos);

	QMenu menu(mw);
	QAction *aNew = menu.addAction(T("Neue leere Seite"));
	QAction *aDup = nullptr, *aRen = nullptr, *aDel = nullptr;
	if (idx >= 0) {
		aDup = menu.addAction(T("Seite duplizieren"));
		aRen = menu.addAction(T("Umbenennen …"));
		menu.addSeparator();
		aDel = menu.addAction(T("Seite löschen"));
	}

	QAction *chosen = menu.exec(tabs->mapToGlobal(pos));
	if (!chosen)
		return;
	if (chosen == aNew)
		addEmptyPage();
	else if (chosen == aDup)
		duplicatePage(idx);
	else if (chosen == aRen)
		renamePage(idx);
	else if (chosen == aDel)
		removePage(idx);
}

/* ------------------------------------------------------------------ */
/* Seitenlogik                                                         */
/* ------------------------------------------------------------------ */

void DockPages::captureCurrent()
{
	if (!ready || current < 0 || current >= pages.size())
		return;
	pages[current].state = mw->saveState(STATE_VERSION);
}

void DockPages::applyPreviewVisibility(bool hide)
{
	// Das zentrale Widget von OBS enthält Vorschau und Kontextleiste.
	// Ist es ausgeblendet, nutzen die Docks den gesamten Platz.
	if (QWidget *central = mw->centralWidget())
		central->setVisible(!hide);

	if (previewBtn) {
		QSignalBlocker block(previewBtn);
		previewBtn->setChecked(!hide);
	}
}

void DockPages::applyPage(int index)
{
	if (index < 0 || index >= pages.size())
		return;

	// Zuerst die Vorschau setzen, damit die Dock-Größen danach passen
	applyPreviewVisibility(pages[index].hidePreview);

	const QByteArray &st = pages[index].state;
	if (!st.isEmpty())
		mw->restoreState(st, STATE_VERSION);
	toolbar->show(); // Seitenleiste immer sichtbar halten
}

void DockPages::setPreviewHidden(bool hide)
{
	if (!ready || current < 0 || current >= pages.size()) {
		applyPreviewVisibility(false);
		return;
	}

	pages[current].hidePreview = hide;
	applyPreviewVisibility(hide);

	// Layout erst nach dem Neuaufbau durch Qt speichern
	QTimer::singleShot(0, this, [this] {
		captureCurrent();
		save();
	});
}

void DockPages::switchTo(int index)
{
	if (!ready || index < 0 || index >= pages.size() || index == current)
		return;
	captureCurrent();
	current = index;
	applyPage(current);
	save();
}

void DockPages::addEmptyPage()
{
	if (!ready)
		return;

	bool ok = false;
	QString name = QInputDialog::getText(mw, T("Neue Seite"), T("Name der Seite:"), QLineEdit::Normal,
					     T("Seite %1").arg(pages.size() + 1), &ok);
	if (!ok)
		return;
	name = name.trimmed();
	if (name.isEmpty())
		name = T("Seite %1").arg(pages.size() + 1);

	captureCurrent();

	// Alle Docks ausblenden; über das Menü "Docks" kannst du sie
	// anschließend gezielt auf dieser Seite einblenden.
	// setVisible(false) statt close(), damit OBS keinen Hinweisdialog zeigt.
	const auto docks = mw->findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);
	for (QDockWidget *d : docks)
		d->setVisible(false);
	applyPreviewVisibility(false);

	DockPage page;
	page.name = name;
	page.state = mw->saveState(STATE_VERSION);
	pages.push_back(page);

	current = int(pages.size()) - 1;
	rebuildTabs();
	save();
}

void DockPages::duplicatePage(int index)
{
	if (!ready || index < 0 || index >= pages.size())
		return;

	captureCurrent();
	DockPage copy = pages[index];
	copy.name += T(" (Kopie)");
	pages.insert(index + 1, copy);

	current = index + 1;
	applyPage(current);
	rebuildTabs();
	save();
}

void DockPages::renamePage(int index)
{
	if (index < 0 || index >= pages.size())
		return;

	bool ok = false;
	QString name = QInputDialog::getText(mw, T("Seite umbenennen"), T("Neuer Name:"), QLineEdit::Normal,
					     pages[index].name, &ok);
	name = name.trimmed();
	if (!ok || name.isEmpty())
		return;

	pages[index].name = name;
	tabs->setTabText(index, name);
	captureCurrent();
	save();
}

void DockPages::removePage(int index)
{
	if (!ready || index < 0 || index >= pages.size())
		return;

	if (pages.size() <= 1) {
		QMessageBox::information(mw, T("Dock-Seiten"), T("Die letzte Seite kann nicht gelöscht werden."));
		return;
	}

	if (QMessageBox::question(mw, T("Seite löschen"), T("Seite \"%1\" wirklich löschen?").arg(pages[index].name)) !=
	    QMessageBox::Yes)
		return;

	const bool wasCurrent = (index == current);
	if (!wasCurrent)
		captureCurrent();

	pages.removeAt(index);

	if (wasCurrent) {
		current = qMin(index, int(pages.size()) - 1);
		applyPage(current);
	} else if (index < current) {
		current--;
	}

	rebuildTabs();
	save();
}

void DockPages::onTabMoved()
{
	current = tabs->currentIndex();
	captureCurrent();
	save();
}

/* ------------------------------------------------------------------ */
/* Hotkeys                                                             */
/* ------------------------------------------------------------------ */

static void hotkey_cb(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	// Hotkeys laufen in einem eigenen Thread -> in den UI-Thread wechseln
	auto *self = static_cast<DockPages *>(data);
	QMetaObject::invokeMethod(self, [self, id] { self->handleHotkey(id); }, Qt::QueuedConnection);
}

void DockPages::registerHotkeys()
{
	auto add = [this](const QString &name, const QString &desc) {
		DockPageHotkey hk;
		hk.name = name.toUtf8();
		hk.id = obs_hotkey_register_frontend(hk.name.constData(), desc.toUtf8().constData(), hotkey_cb, this);
		hotkeys.push_back(hk);
	};

	add(QStringLiteral("DockPages.Prev"), T("Dock-Seiten: Vorherige Seite"));
	add(QStringLiteral("DockPages.Next"), T("Dock-Seiten: Nächste Seite"));
	for (int i = 1; i <= 9; i++)
		add(QStringLiteral("DockPages.Page%1").arg(i), T("Dock-Seiten: Seite %1").arg(i));
}

void DockPages::unregisterHotkeys()
{
	for (auto &hk : hotkeys) {
		if (hk.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(hk.id);
		hk.id = OBS_INVALID_HOTKEY_ID;
	}
}

void DockPages::handleHotkey(obs_hotkey_id id)
{
	if (!ready || pages.isEmpty())
		return;

	int which = -1;
	for (int i = 0; i < int(hotkeys.size()); i++) {
		if (hotkeys[i].id == id) {
			which = i;
			break;
		}
	}
	if (which < 0)
		return;

	const int n = int(pages.size());
	int target = -1;
	if (which == 0)
		target = (current - 1 + n) % n;
	else if (which == 1)
		target = (current + 1) % n;
	else if (which - 2 < n)
		target = which - 2;

	if (target >= 0 && target != current)
		tabs->setCurrentIndex(target); // löst switchTo() aus
}

/* ------------------------------------------------------------------ */
/* OBS-Ereignisse                                                      */
/* ------------------------------------------------------------------ */

void DockPages::onFinishedLoading()
{
	// Browser- und Plugin-Docks entstehen teils erst kurz nach dem Laden
	QTimer::singleShot(500, this, [this] {
		ready = true;
		if (pages.isEmpty()) {
			DockPage page;
			page.name = T("Seite 1");
			page.state = mw->saveState(STATE_VERSION);
			pages.push_back(page);
			current = 0;
			rebuildTabs();
			save();
		} else {
			applyPage(current);
		}
	});
}

void DockPages::onExit()
{
	captureCurrent();
	save();
	unregisterHotkeys();
}

/* ------------------------------------------------------------------ */
/* Speichern / Laden                                                   */
/* ------------------------------------------------------------------ */

void DockPages::load()
{
	char *path = obs_module_config_path("pages.json");
	if (!path)
		return;
	obs_data_t *data = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!data)
		return;

	obs_data_array_t *arr = obs_data_get_array(data, "pages");
	const size_t n = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < n; i++) {
		obs_data_t *item = obs_data_array_item(arr, i);
		DockPage p;
		p.name = QString::fromUtf8(obs_data_get_string(item, "name"));
		p.state = QByteArray::fromBase64(obs_data_get_string(item, "state"));
		p.hidePreview = obs_data_get_bool(item, "hidePreview");
		if (p.name.isEmpty())
			p.name = T("Seite %1").arg(i + 1);
		pages.push_back(p);
		obs_data_release(item);
	}
	obs_data_array_release(arr);

	current = int(obs_data_get_int(data, "current"));
	if (current < 0 || current >= pages.size())
		current = 0;

	obs_data_t *hkData = obs_data_get_obj(data, "hotkeys");
	if (hkData) {
		for (const auto &hk : hotkeys) {
			obs_data_array_t *bindings = obs_data_get_array(hkData, hk.name.constData());
			if (bindings && hk.id != OBS_INVALID_HOTKEY_ID)
				obs_hotkey_load(hk.id, bindings);
			obs_data_array_release(bindings);
		}
		obs_data_release(hkData);
	}

	obs_data_release(data);
}

void DockPages::save() const
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	char *path = obs_module_config_path("pages.json");
	if (!path)
		return;

	obs_data_t *data = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();

	for (const auto &p : pages) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "name", p.name.toUtf8().constData());
		obs_data_set_string(item, "state", p.state.toBase64().constData());
		obs_data_set_bool(item, "hidePreview", p.hidePreview);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}

	obs_data_set_array(data, "pages", arr);
	obs_data_set_int(data, "current", current);

	obs_data_t *hkData = obs_data_create();
	for (const auto &hk : hotkeys) {
		if (hk.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *bindings = obs_hotkey_save(hk.id);
		obs_data_set_array(hkData, hk.name.constData(), bindings);
		obs_data_array_release(bindings);
	}
	obs_data_set_obj(data, "hotkeys", hkData);
	obs_data_release(hkData);

	obs_data_save_json_safe(data, path, "tmp", "bak");

	obs_data_array_release(arr);
	obs_data_release(data);
	bfree(path);
}
