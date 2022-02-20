// SPDX-License-Identifier: GPL-2.0-only
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include "buffer.h"
#include "labwc.h"

#define INDENT_SIZE (3)

static char *
get_node_type(enum wlr_scene_node_type type)
{
	switch (type) {
	case WLR_SCENE_NODE_ROOT:
		return "root";
	case WLR_SCENE_NODE_TREE:
		return "tree";
	case WLR_SCENE_NODE_SURFACE:
		return "surface";
	case WLR_SCENE_NODE_RECT:
		return "rect";
	case WLR_SCENE_NODE_BUFFER:
		return "buffer";
	}
	return "error";
}

static void
dump_tree(struct wlr_scene_node *node, int pos, int x, int y)
{
	char *type = get_node_type(node->type);

	if (pos) {
		printf("%*c+-- ", pos, ' ');
	}
	printf("%s (%d,%d) [%p]\n", type, x, y, node);

	struct wlr_scene_node *child;
	wl_list_for_each(child, &node->state.children, state.link) {
		dump_tree(child, pos + INDENT_SIZE, x + child->state.x,
			y + child->state.y);
	}
}

static char *
get_layer_name(uint32_t layer)
{
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return "background";
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return "bottom";
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return "top";
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return "overlay";
	default:
		abort();
	}
}

void
debug_dump_scene(struct server *server)
{
	struct wlr_scene_node *node;

	printf(":: view_tree ::\n");
	node = &server->view_tree->node;
	dump_tree(node, 0, node->state.x, node->state.y);

	printf(":: layer_tree ::\n");
	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		for (int i = 0; i < 4; i++) {
			node = &output->layer_tree[i]->node;
			printf("layer-%s\n", get_layer_name(i));
			dump_tree(node, 0, node->state.x, node->state.y);
		}
	}

	printf(":: osd_tree ::\n");
	node = &server->osd_tree->node;
	dump_tree(node, 0, node->state.x, node->state.y);

	printf(":: menu_tree ::\n");
	node = &server->menu_tree->node;
	dump_tree(node, 0, node->state.x, node->state.y);
}
