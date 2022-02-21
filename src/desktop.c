// SPDX-License-Identifier: GPL-2.0-only
#include "config.h"
#include <assert.h>
#include "labwc.h"
#include "layers.h"
#include "ssd.h"

static void
move_to_front(struct view *view)
{
	wl_list_remove(&view->link);
	wl_list_insert(&view->server->views, &view->link);
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
}

#if HAVE_XWAYLAND
static struct wlr_xwayland_surface *
top_parent_of(struct view *view)
{
	struct wlr_xwayland_surface *s = view->xwayland_surface;
	while (s->parent) {
		s = s->parent;
	}
	return s;
}

static void
move_xwayland_sub_views_to_front(struct view *parent)
{
	if (!parent || parent->type != LAB_XWAYLAND_VIEW) {
		return;
	}
	struct view *view, *next;
	wl_list_for_each_reverse_safe(view, next, &parent->server->views, link)
	{
		/* need to stop here, otherwise loops keeps going forever */
		if (view == parent) {
			break;
		}
		if (view->type != LAB_XWAYLAND_VIEW) {
			continue;
		}
		if (!view->mapped && !view->minimized) {
			continue;
		}
		if (top_parent_of(view) != parent->xwayland_surface) {
			continue;
		}
		move_to_front(view);
		/* TODO: we should probably focus on these too here */
	}
}
#endif

void
desktop_move_to_front(struct view *view)
{
	if (!view) {
		return;
	}
	move_to_front(view);
#if HAVE_XWAYLAND
	move_xwayland_sub_views_to_front(view);
#endif
}

static void
wl_list_insert_tail(struct wl_list *list, struct wl_list *elm)
{
	elm->prev = list->prev;
	elm->next = list;
	list->prev = elm;
	elm->prev->next = elm;
}

void
desktop_move_to_back(struct view *view)
{
	if (!view) {
		return;
	}
	wl_list_remove(&view->link);
	wl_list_insert_tail(&view->server->views, &view->link);
}

static void
deactivate_all_views(struct server *server)
{
	struct view *view;
	wl_list_for_each (view, &server->views, link) {
		if (!view->mapped) {
			continue;
		}
		view_set_activated(view, false);
	}
}

void
desktop_focus_and_activate_view(struct seat *seat, struct view *view)
{
	if (!view) {
		seat_focus_surface(seat, NULL);
		return;
	}

	/*
	 * Guard against views with no mapped surfaces when handling
	 * 'request_activate' and 'request_minimize'.
	 * See notes by isfocusable()
	 */
	if (!view->surface) {
		return;
	}

	if (input_inhibit_blocks_surface(seat, view->surface->resource)) {
		return;
	}

	if (view->minimized) {
		/*
		 * Unminimizing will map the view which triggers a call to this
		 * function again.
		 */
		view_minimize(view, false);
		return;
	}

	if (!view->mapped) {
		return;
	}

	struct wlr_surface *prev_surface;
	prev_surface = seat->seat->keyboard_state.focused_surface;

	/* Do not re-focus an already focused surface. */
	if (prev_surface == view->surface) {
		return;
	}

	deactivate_all_views(view->server);
	view_set_activated(view, true);
	seat_focus_surface(seat, view->surface);
}

/*
 * Some xwayland apps produce unmapped surfaces on startup and also leave
 * some unmapped surfaces kicking around on 'close' (for example leafpad's
 * "about" dialogue). Whilst this is not normally a problem, we have to be
 * careful when cycling between views. The only views we should focus are
 * those that are already mapped and those that have been minimized.
 */
bool
isfocusable(struct view *view)
{
	/* filter out those xwayland surfaces that have never been mapped */
	if (!view->surface) {
		return false;
	}
	return (view->mapped || view->minimized);
}

static bool
has_focusable_view(struct wl_list *wl_list)
{
	struct view *view;
	wl_list_for_each (view, wl_list, link) {
		if (isfocusable(view)) {
			return true;
		}
	}
	return false;
}

static struct view *
first_view(struct server *server)
{
	struct view *view;
	view = wl_container_of(server->views.next, view, link);
	return view;
}

struct view *
desktop_cycle_view(struct server *server, struct view *current,
		enum lab_cycle_dir dir)
{
	if (!has_focusable_view(&server->views)) {
		return NULL;
	}

	struct view *view = current ? current : first_view(server);
	if (dir == LAB_CYCLE_DIR_FORWARD) {
		/* Replacement for wl_list_for_each_from() */
		do {
			view = wl_container_of(view->link.next, view, link);
		} while (&view->link == &server->views || !isfocusable(view));
	} else if (dir == LAB_CYCLE_DIR_BACKWARD) {
		do {
			view = wl_container_of(view->link.prev, view, link);
		} while (&view->link == &server->views || !isfocusable(view));
	}
	damage_all_outputs(server);
	return view;
}

static bool
has_mapped_view(struct wl_list *wl_list)
{
	struct view *view;
	wl_list_for_each (view, wl_list, link) {
		if (view->mapped) {
			return true;
		}
	}
	return false;
}

static struct view *
topmost_mapped_view(struct server *server)
{
	if (!has_mapped_view(&server->views)) {
		return NULL;
	}

	/* start from tail of server->views */
	struct view *view = wl_container_of(server->views.prev, view, link);
	do {
		view = wl_container_of(view->link.next, view, link);
	} while (&view->link == &server->views || !view->mapped);
	return view;
}

struct view *
desktop_focused_view(struct server *server)
{
	struct seat *seat = &server->seat;
	struct wlr_surface *focused_surface;
	focused_surface = seat->seat->keyboard_state.focused_surface;
	if (!focused_surface) {
		return NULL;
	}
	struct view *view;
	wl_list_for_each (view, &server->views, link) {
		if (view->surface == focused_surface) {
			return view;
		}
	}

	return NULL;
}

void
desktop_focus_topmost_mapped_view(struct server *server)
{
	struct view *view = topmost_mapped_view(server);
	desktop_focus_and_activate_view(&server->seat, view);
	desktop_move_to_front(view);
}

struct view *
desktop_node_and_view_at(struct server *server, double lx, double ly,
		struct wlr_scene_node **scene_node, double *sx, double *sy,
		enum ssd_part_type *view_area)
{
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->node, lx, ly, sx, sy);

	*scene_node = node;
	if (!node) {
		*view_area = LAB_SSD_NONE;
		return NULL;
	}
	if (node->type == WLR_SCENE_NODE_SURFACE) {
		struct wlr_surface *surface =
			wlr_scene_surface_from_node(node)->surface;
		if (wlr_surface_is_layer_surface(surface)) {
			*view_area = LAB_SSD_LAYER_SURFACE;
			return NULL;
		}
	}
	struct wlr_scene_node *osd = &server->osd_tree->node;
	struct wlr_scene_node *menu = &server->menu_tree->node;
	while (node && !node->data) {
		if (node == osd) {
			*view_area = LAB_SSD_OSD;
			return NULL;
		} else if (node == menu) {
			*view_area = LAB_SSD_MENU;
			return NULL;
		}
		node = node->parent;
	}
	if (!node) {
		wlr_log(WLR_ERROR, "Unknown node detected");
		*view_area = LAB_SSD_NONE;
		return NULL;
	}
	struct view *view = node->data;
	*view_area = ssd_get_part_type(view, *scene_node);
	return view;
}

struct view *
desktop_view_at_cursor(struct server *server)
{
	double sx, sy;
	struct wlr_scene_node *node;
	enum ssd_part_type view_area = LAB_SSD_NONE;

	return desktop_node_and_view_at(server,
			server->seat.cursor->x, server->seat.cursor->y,
			&node, &sx, &sy, &view_area);
}
