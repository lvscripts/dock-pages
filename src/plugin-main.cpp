#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMainWindow>

#include "dock-pages.hpp"

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Mehrere Seiten (Layouts) zum Platzieren von Docks";
}

static DockPages *g_pages = nullptr;

static void frontend_event(enum obs_frontend_event event, void *)
{
	if (!g_pages)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		g_pages->onFinishedLoading();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		g_pages->onExit();
		g_pages = nullptr; // wird mit dem Hauptfenster zerstört
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	auto *mw = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mw) {
		blog(LOG_ERROR, "[dock-pages] Hauptfenster nicht gefunden");
		return false;
	}

	g_pages = new DockPages(mw); // Parent = Hauptfenster
	obs_frontend_add_event_callback(frontend_event, nullptr);

	blog(LOG_INFO, "[dock-pages] geladen");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	g_pages = nullptr;
}
