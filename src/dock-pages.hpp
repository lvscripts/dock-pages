#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QPoint>

class QMainWindow;
class QToolBar;
class QTabBar;

struct DockPage {
	QString name;
	QByteArray state; // Ergebnis von QMainWindow::saveState()
};

class DockPages : public QObject {
public:
	explicit DockPages(QMainWindow *mainWindow);

	void onFinishedLoading();
	void onExit();

private:
	void buildToolbar();
	void rebuildTabs();

	void switchTo(int index);
	void captureCurrent();
	void applyPage(int index);

	void addEmptyPage();
	void duplicatePage(int index);
	void renamePage(int index);
	void removePage(int index);
	void onTabMoved();
	void showContextMenu(const QPoint &pos);

	void load();
	void save() const;

	QMainWindow *mw;
	QToolBar *toolbar = nullptr;
	QTabBar *tabs = nullptr;

	QList<DockPage> pages;
	int current = 0;
	bool ready = false;        // erst nach vollständigem Laden von OBS
	bool updatingTabs = false; // verhindert Rückkopplung beim Neuaufbau
};
