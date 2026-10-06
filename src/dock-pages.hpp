#pragma once

#include <obs.h>

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QPoint>

class QMainWindow;
class QToolBar;
class QTabBar;
class QToolButton;

struct DockPage {
	QString name;
	QByteArray state;         // Ergebnis von QMainWindow::saveState()
	bool hidePreview = false; // Vorschau auf dieser Seite ausgeblendet
};

struct DockPageHotkey {
	QByteArray name;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

class DockPages : public QObject {
public:
	explicit DockPages(QMainWindow *mainWindow);

	void onFinishedLoading();
	void onExit();
	void handleHotkey(obs_hotkey_id id);

private:
	void buildToolbar();
	void rebuildTabs();

	void switchTo(int index);
	void captureCurrent();
	void applyPage(int index);
	void applyPreviewVisibility(bool hide);
	void setPreviewHidden(bool hide);

	void addEmptyPage();
	void duplicatePage(int index);
	void renamePage(int index);
	void removePage(int index);
	void onTabMoved();
	void showContextMenu(const QPoint &pos);

	void registerHotkeys();
	void unregisterHotkeys();

	void load();
	void save() const;

	QMainWindow *mw;
	QToolBar *toolbar = nullptr;
	QTabBar *tabs = nullptr;
	QToolButton *previewBtn = nullptr;

	QList<DockPage> pages;
	QList<DockPageHotkey> hotkeys; // 0: zurück, 1: weiter, 2..10: Seite 1..9
	int current = 0;
	bool ready = false;        // erst nach vollständigem Laden von OBS
	bool updatingTabs = false; // verhindert Rückkopplung beim Neuaufbau
};
