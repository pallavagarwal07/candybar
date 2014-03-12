#include "widgets.h"
#include "desktops.h"

static int
widget_update (struct widget *widget, xcb_ewmh_connection_t *ewmh, int screen_nbr) {
	unsigned short i;
	uint32_t desktop_curr, desktop_len, client_desktop;
	xcb_ewmh_get_windows_reply_t clients;
	xcb_icccm_wm_hints_t window_hints;
	struct desktop *desktops = calloc(DESKTOP_MAX_LEN, sizeof(struct desktop));

	/* get current desktop */
	if (!xcb_ewmh_get_current_desktop_reply(ewmh, xcb_ewmh_get_current_desktop_unchecked(ewmh, screen_nbr), &desktop_curr, NULL)) {
		LOG_INFO("ewmh: could not get current desktop");

		return 1;
	}

	/* get desktop count */
	if (!xcb_ewmh_get_number_of_desktops_reply(ewmh, xcb_ewmh_get_number_of_desktops_unchecked(ewmh, screen_nbr), &desktop_len, NULL)) {
		LOG_INFO("ewmh: could not get desktop count");

		return 2;
	}

	for (i = 0; i < desktop_len; i++) {
		desktops[i].is_selected = i == desktop_curr;
		desktops[i].is_valid = true;
		desktops[i].is_urgent = false;
		desktops[i].clients_len = 0;
	}

	/* get clients */
	if (!xcb_ewmh_get_client_list_reply(ewmh, xcb_ewmh_get_client_list_unchecked(ewmh, screen_nbr), &clients, NULL)) {
		LOG_INFO("ewmh: could not get client list");

		return 3;
	}

	for (i = 0; i < clients.windows_len; i++) {
		if (!xcb_ewmh_get_wm_desktop_reply(ewmh, xcb_ewmh_get_wm_desktop_unchecked(ewmh, clients.windows[i]), &client_desktop, NULL)) {
			/* window isn't associated with a desktop */
			continue;
		}
		desktops[client_desktop].clients_len++;

		/* check icccm urgency hint on client */
		if (!xcb_icccm_get_wm_hints_reply(ewmh->connection, xcb_icccm_get_wm_hints_unchecked(ewmh->connection, clients.windows[i]), &window_hints, NULL)) {
			LOG_INFO("icccm: could not get window hints");
		}
		if (window_hints.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			desktops[client_desktop].is_urgent = true;
		}
	}

	xcb_ewmh_get_windows_reply_wipe(&clients);

	json_t *json_data_object = json_object();
	json_t *json_desktops_array = json_array();
	json_object_set_new(json_data_object, "desktops", json_desktops_array);

	for (i = 0; i < DESKTOP_MAX_LEN; i++) {
		if (!desktops[i].is_valid) {
			continue;
		}

		json_t *json_desktop = json_object();
		json_object_set_new(json_desktop, "clients_len", json_integer(desktops[i].clients_len));
		json_object_set_new(json_desktop, "is_urgent", json_boolean(desktops[i].is_urgent));
		json_array_append_new(json_desktops_array, json_desktop);

		if (desktops[i].is_selected) {
			json_object_set_new(json_data_object, "current_desktop", json_integer(i));
		}
	}

	widget_send_update(json_data_object, widget);

	free(desktops);

	return 0;
}

static void
widget_cleanup (void *arg) {
	LOG_DEBUG("cleanup");

	void **cleanup_data = arg;

	xcb_ewmh_connection_t *ewmh = cleanup_data[0];
	xcb_disconnect(ewmh->connection);
	xcb_ewmh_connection_wipe(ewmh);
	free(arg);
}

void*
widget_init (struct widget *widget) {
	LOG_DEBUG("init");

	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn)) {
		LOG_ERR("could not connect to display %s.", getenv("DISPLAY"));

		return 0;
	}

	int screen_nbr = 0; /* FIXME load from config */
	xcb_ewmh_connection_t *ewmh = malloc(sizeof(xcb_ewmh_connection_t));
	xcb_intern_atom_cookie_t *ewmh_cookie = xcb_ewmh_init_atoms(conn, ewmh);
	xcb_ewmh_init_atoms_replies(ewmh, ewmh_cookie, NULL);

	uint32_t values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
	xcb_generic_event_t *evt;
	xcb_generic_error_t *err = xcb_request_check(ewmh->connection,
	                                             xcb_change_window_attributes_checked(ewmh->connection,
	                                                                                  ewmh->screens[screen_nbr]->root,
	                                                                                  XCB_CW_EVENT_MASK,
	                                                                                  values));

	if (err != NULL) {
		LOG_ERR("could not request EWMH property change notifications");

		return 0;
	}

	void **cleanup_data = malloc(sizeof(void*) * 1);
	cleanup_data[0] = ewmh;

	pthread_cleanup_push(widget_cleanup, cleanup_data);
	widget_update(widget, ewmh, screen_nbr);
	for (;;) {
		while ((evt = xcb_wait_for_event(ewmh->connection)) != NULL) {
			xcb_property_notify_event_t *pne;
			switch (XCB_EVENT_RESPONSE_TYPE(evt)) {
			case XCB_PROPERTY_NOTIFY:
				pne = (xcb_property_notify_event_t*)evt;
				if (pne->atom == ewmh->_NET_DESKTOP_NAMES) {
					widget_update(widget, ewmh, screen_nbr);
				}
				else if (pne->atom == ewmh->_NET_NUMBER_OF_DESKTOPS) {
					widget_update(widget, ewmh, screen_nbr);
				}
				else if (pne->atom == ewmh->_NET_CURRENT_DESKTOP) {
					widget_update(widget, ewmh, screen_nbr);
				}
			default:
				break;
			}
			free(evt);
		}
	}

	pthread_cleanup_pop(1);
}
