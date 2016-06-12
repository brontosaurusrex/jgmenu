/*
 * jgmenu.c
 *
 * Copyright (C) Johan Malm 2014
 *
 * jgmenu is a stand-alone menu which reads the menu items from stdin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include "x11-ui.h"
#include "config.h"
#include "util.h"
#include "geometry.h"
#include "prog-finder.h"

#define MAX_FIELDS 3		/* nr fields to parse for each stdin line */

#define MOUSE_FUDGE 3		/* temporary offset */
				/* Not sure why I need that offset... */

struct Item {
	char *t[MAX_FIELDS];	/* pointers name, cmd			  */
	char *tag;		/* used to tag the start of a submenu	  */
	struct Area area;
	struct Item *next, *prev;
};

/*
 * All menu items in the input file are stored in a dynamic array (=vector).
 * These items are also joined in a doubly linked list.
 *
 * When a submenu is checked out, *subhead and *subtail are set.
 *
 * *first and *last point to the first/last visible menu items (i.e. what can
 * pysically be seen on the screen.) 
 * The "number of visible menu items" is not a variable in the Menu struct,
 * but can be got by calling geo_get_nr_visible_items().
 */
struct Menu {
	struct Item *head;	/* first item in linked list		  */
	struct Item *tail;	/* last item in linked list		  */
	int nr_items_in_list;

	struct Item *subhead;	/* first item in checked out submenu	  */
	struct Item *subtail;	/* first item in checked out submenu	  */
	int nr_items_in_submenu;

	struct Item *first;	/* first visible item			  */
	struct Item *last;	/* last visible item			  */
	struct Item *sel;	/* currently selected item		  */

	char *title;
};

struct Menu menu;

void usage(void)
{
	printf("Usage: jgmenu [OPTIONS]\n"
	       "    --version             show version\n"
	       "    --no-spawn            redirect command to stdout rather than executing it\n"
	       "    --checkout=<tag>      checkout submenu <tag> on startup\n"
	       "    --config-file=<file>  read config file\n");
	exit(0);
}

void init_menuitem_coordinates(void)
{
	struct Item *p;
	int i = 0;

	if (menu.title)
		++i;

	for (p = menu.first; p && p->t[0] && p->prev != menu.last; p++) {
		p->area = geo_get_item_coordinates(i);
		++i;
	}
}

void draw_menu(void)
{
	struct Item *p;
	int w, h;

	h = geo_get_item_height();
	w = geo_get_menu_width();

	/* Draw background */
	ui_clear_canvas();
	ui_draw_rectangle(0, 0, w, geo_get_menu_height(), config.menu_radius,
			  0.0, 1, config.color_menu_bg);

	/* Draw title */
	if (menu.title) {
		ui_draw_rectangle_rounded_at_top(0, 0, w, h, config.menu_radius,
						 0.0, 1, config.color_title_bg);
		ui_insert_text(menu.title, config.item_padding_x, 0, h,
			       config.color_norm_fg);
	}

	/* Draw menu border */
	ui_draw_rectangle(0, 0, w, geo_get_menu_height(), config.menu_radius,
			  config.menu_border, 0, config.color_menu_fg);

	/* Draw menu items */
	for (p = menu.first; p && p->t[0] && p->prev != menu.last; p++) {
		if (p == menu.sel) {
			ui_draw_rectangle(p->area.x, p->area.y, p->area.w,
					  p->area.h, config.item_radius, 0.0, 1,
					  config.color_sel_bg);
			ui_draw_rectangle(p->area.x, p->area.y, p->area.w,
					  p->area.h, config.item_radius,
					  config.item_border, 0,
					  config.color_sel_fg);
		} else {
			ui_draw_rectangle(p->area.x, p->area.y, p->area.w,
					  p->area.h, config.item_radius, 0.0, 1,
					  config.color_norm_bg);
			ui_draw_rectangle(p->area.x, p->area.y, p->area.w,
					  p->area.h, config.item_radius,
					  config.item_border, 0,
					  config.color_norm_fg);
		}

		/* Draw submenu arrow */
		if ((!strncmp(p->t[1], "^checkout(", 10) && strncmp(p->t[0], "..", 2)) ||
		     !strncmp(p->t[1], "^sub(", 5))
			ui_insert_text("→", p->area.x + p->area.w -
				       config.item_padding_x - 10, p->area.y,
				       p->area.h, config.color_norm_fg);

		if (strncmp(p->t[1], "^checkout(", 10) &&
		    strncmp(p->t[1], "^sub(", 5) &&
		    strncmp(p->t[0], "..", 2) &&
		    !is_prog(p->t[1]))
			/* Set color for programs not available in $PATH */
			ui_insert_text(p->t[0], p->area.x +
				       config.item_padding_x, p->area.y,
				       p->area.h, config.color_noprog_fg);
		else if (p == menu.sel)
			ui_insert_text(p->t[0], p->area.x +
				       config.item_padding_x, p->area.y,
				       p->area.h, config.color_sel_fg);
		else
			ui_insert_text(p->t[0], p->area.x +
				       config.item_padding_x, p->area.y,
				       p->area.h, config.color_norm_fg);
	}

	ui_map_window(geo_get_menu_width(), geo_get_menu_height());
}

/*
 * Sets *menu.first and *menu.last pointing to the beginnning and end of
 * the submenu
 */
void checkout_submenu(char *tag)
{
	struct Item *item;
	char *tagtok = "^tag(";

	menu.first = NULL;
	menu.title = NULL;

	if (!tag) {
		menu.first = menu.head;
	} else {
		for (item = menu.head; item && item->t[0]; item++) {
			if (item->tag && !strncmp(item->tag, tag, strlen(tag))) {
				menu.title = item->t[0];
				if (item->next)
					menu.first = item + 1;
				else
					menu.first = NULL;
				break;
			}
		}

		if (!menu.title)
			die("menu.title pointer not set.  Tag not found.");
		if (!menu.first)
			die("menu.first not set. Menu has no content");
	}

	menu.last = NULL;

	if (!menu.first->next) {
		menu.last = menu.first;
	} else {
		item = menu.first->next;

		while (!menu.last && item && item->t[0]) {
			if (!item->next || !strncmp(item->next->t[1], tagtok, strlen(tagtok)))
				menu.last = item;
			else
				item++;
		}

		if (!menu.last)
			die("menu.last pointer not set");
	}

	menu.nr_items_in_submenu = 1;
	for (item = menu.first; item && item->t[0] && item != menu.last; item++)
		menu.nr_items_in_submenu++;

	/*
	 * menu.subhead remembers where the submenu starts.
	 * menu.first will change with scrolling
	 */
	/* FIXME - subhead and subtail should be used above for first/last
	 * only initiated at the end of this function
	 */
	menu.subhead = menu.first;
	menu.subtail = menu.last;

	menu.sel = menu.first;

	geo_set_show_title(menu.title);
	if (config.max_items < menu.nr_items_in_submenu)
		geo_set_nr_visible_items(config.max_items);
	else if (config.min_items > menu.nr_items_in_submenu)
		geo_set_nr_visible_items(config.min_items);
	else
		geo_set_nr_visible_items(menu.nr_items_in_submenu);

	if (config.min_items > menu.nr_items_in_submenu)
		menu.last = menu.first + menu.nr_items_in_submenu - 1;
	else
		menu.last = menu.first + geo_get_nr_visible_items() - 1;
}

char *parse_caret_action(char *s, char *token)
{
	/* Returns bar from ^foo(bar)
	 * s="^foo(bar)"
	 * token="^foo("
	 */

	char *p, *q;

	p = NULL;
	q = NULL;

	if (s)
		if (strncmp(s, token, strlen(token)) == 0) {
			p = strdup(s);
			p += strlen(token);
			q = strchr(p, ')');
			if (q)
				*q = '\0';
		}

	return p;
}

void action_cmd(char *cmd)
{
	char *p = NULL;

	if (!cmd)
		return;
	if (!strncmp(cmd, "^checkout(", 10)) {
		p = parse_caret_action(cmd, "^checkout(");
		if (p) {
			checkout_submenu(p);

			/* menu height has changed - need to redraw window */
			XMoveResizeWindow(ui->dpy, ui->win, geo_get_menu_x0(), geo_get_menu_y0(),
					  geo_get_menu_width(), geo_get_menu_height());

			init_menuitem_coordinates();
			draw_menu();
		}
	} else if (!strncmp(cmd, "^sub(", 5)) {
		p = parse_caret_action(cmd, "^sub(");
		if (p) {
			spawn(p);
			exit(0);
		}
	} else {
		spawn(cmd);
		exit(0);
	}
}

int scroll_step_down()
{
	if (menu.subtail - menu.last < geo_get_nr_visible_items())
		return (menu.subtail - menu.last);
	else
		return geo_get_nr_visible_items();
}

int scroll_step_up()
{
	if (menu.first - menu.subhead < geo_get_nr_visible_items())
		return (menu.first - menu.subhead);
	else
		return geo_get_nr_visible_items();
}

void key_event(XKeyEvent *ev)
{
	char buf[32];
	KeySym ksym = NoSymbol;
	Status status;

	XmbLookupString(ui->xic, ev, buf, sizeof(buf), &ksym, &status);
	if (status == XBufferOverflow)
		return;
	switch (ksym) {
	default:
		break;
	case XK_End:
		if (menu.nr_items_in_submenu > geo_get_nr_visible_items()) {
			menu.first = menu.subtail - geo_get_nr_visible_items() + 1;
			menu.last = menu.subtail;
			init_menuitem_coordinates();
		}
		menu.sel = menu.last;
		break;
	case XK_Escape:
		exit(0);
	case XK_Home:
		if (menu.nr_items_in_submenu > geo_get_nr_visible_items()) {
			menu.first = menu.subhead;
			menu.last = menu.subhead + geo_get_nr_visible_items() - 1;
			init_menuitem_coordinates();
		}
		menu.sel = menu.first;
		break;
	case XK_Up:
		if (!menu.sel || !menu.sel->prev)
			break;
		if (menu.sel != menu.first) {
			menu.sel = menu.sel->prev;
		} else if (menu.sel == menu.first && menu.first != menu.subhead) {
			menu.first = menu.first->prev;
			menu.last = menu.last->prev;
			menu.sel = menu.first;
			init_menuitem_coordinates();
		}
		break;
	case XK_Next:	/* PageDown */
		if (menu.nr_items_in_submenu > geo_get_nr_visible_items()) {
			menu.first += scroll_step_down();
			menu.last = menu.first + geo_get_nr_visible_items() - 1;
			init_menuitem_coordinates();
		}
		menu.sel = menu.last;
		break;
	case XK_Prior:	/* PageUp */
		if (menu.nr_items_in_submenu > geo_get_nr_visible_items()) {
			menu.first -= scroll_step_up();
			menu.last = menu.first + geo_get_nr_visible_items() - 1;
			init_menuitem_coordinates();
		}
		menu.sel = menu.first;
		break;
	case XK_Return:
	case XK_KP_Enter:
		if (config.spawn)
			action_cmd(menu.sel->t[1]);
		else
			puts(menu.sel->t[1]);
		break;
	case XK_Down:
		if (!menu.sel || !menu.sel->next)
			break;
		if (menu.sel != menu.last) {
			menu.sel = menu.sel->next;
		} else if (menu.sel == menu.last && menu.last != menu.subtail) {
			menu.first = menu.first->next;
			menu.last = menu.last->next;
			menu.sel = menu.last;
			init_menuitem_coordinates();
		}
		break;
	case XK_d:
		config.color_menu_bg[3] += 0.2;
		if (config.color_menu_bg[3] > 1.0)
			config.color_menu_bg[3] = 1.0;
		init_menuitem_coordinates();
		draw_menu();
		break;
	case XK_l:
		config.color_menu_bg[3] -= 0.2;
		if (config.color_menu_bg[3] < 0.0)
			config.color_menu_bg[3] = 0.0;
		init_menuitem_coordinates();
		draw_menu();
		break;
	}
}

void mouse_event(XEvent *e)
{
	struct Item *item;
	XButtonPressedEvent *ev = &e->xbutton;
	int mousex, mousey;
	struct Point mouse_coords;

	mousex = ev->x - geo_get_menu_x0();
	mousey = ev->y - geo_get_menu_y0();
	mouse_coords.x = mousex - MOUSE_FUDGE;
	mouse_coords.y = mousey - MOUSE_FUDGE;

	/* Die if mouse clicked outside window */
	if ((ev->x < geo_get_menu_x0() ||
	     ev->x > geo_get_menu_x0() + geo_get_menu_width() ||
	     ev->y < geo_get_menu_y0() ||
	     ev->y > geo_get_menu_y0() + geo_get_menu_height()) &&
	     (ev->button != Button4 && ev->button != Button5)) {
		ui_cleanup();
		die("Clicked outside menu.");
		return;
	}

	/* right-click */
	if (ev->button == Button3)
		die("Right clicked.");

	/* scroll up */
	if (ev->button == Button4 && menu.sel->prev) {
		if (menu.sel != menu.first) {
			menu.sel = menu.sel->prev;
		} else if (menu.first != menu.subhead) {
			menu.first = menu.first->prev;
			menu.last = menu.last->prev;
			menu.sel = menu.first;
			init_menuitem_coordinates();
		}
		draw_menu();
		return;
	}

	/* scroll down */
	if (ev->button == Button5 && menu.sel->next) {
		if (menu.sel != menu.last) {
			menu.sel = menu.sel->next;
		} else if (menu.last != menu.subtail) {
			menu.first = menu.first->next;
			menu.last = menu.last->next;
			menu.sel = menu.last;
			init_menuitem_coordinates();
		}
		draw_menu();
		return;
	}

	/* left-click */
	if (ev->button == Button1) {
		for (item = menu.first; item && item->t[0] && item->prev != menu.last ; item++) {
			if (ui_is_point_in_area(mouse_coords, item->area)) {
				if (config.spawn) {
					action_cmd(item->t[1]);
					break;
				} else {
					puts(item->t[1]);
				}
			}
		}
	}
}

void tabulate(char *s)
{
	int i = 0;
	int n = 20;		/* column spacing */

	if (s)
		n -= strlen(s);
	else
		n -= 6;		/* because strlen("(null)") = 6 */

	if (n < 0)
		n = 0;

	for (i = 0; i < n; i++)
		printf(" ");
}

void debug_dlist(void)
{
	struct Item *item;

	printf("---------------------------------------------------------------\n");
	printf("Name                Cmd                 Spare               Tag\n");
	printf("---------------------------------------------------------------\n");

	for (item = menu.head; item && item->t[0]; item++) {
		printf("%s", item->t[0]);
		tabulate(item->t[0]);
		printf("%s", item->t[1]);
		tabulate(item->t[1]);
		printf("%s", item->t[2]);
		tabulate(item->t[2]);
		printf("%s", item->tag);
		tabulate(item->tag);
		printf("\n");
	}
	printf("menu.head->t[0]: %s\n", menu.head->t[0]);
	printf("menu.tail->t[0]: %s\n", menu.tail->t[0]);
	printf("menu.first->t[0]: %s\n", menu.first->t[0]);
	printf("menu.sel->t[0]: %s\n", menu.sel->t[0]);
	printf("menu.last->t[0]: %s\n", menu.last->t[0]);
	printf("menu.nr_items_in_submenu: %d\n", menu.nr_items_in_submenu);
}

char *next_field(char *str)
{
	char *tmp;

	tmp = strchr(str, ',');
	if (tmp)
		tmp++;
	return tmp;
}

void parse_csv(struct Item *p)
{
	char *q;
	int j;

	p->t[1] = NULL;
	p->t[2] = NULL;

	for (j = 0; j < MAX_FIELDS - 1; j++)
		if (p->t[j])
			p->t[j+1] = next_field(p->t[j]);

	while ((q = strrchr(p->t[0], ',')))
		*q = '\0';

	/* Prevents seg fault when t[1] == NULL */
	if (!p->t[1])
		p->t[1] = p->t[0];
}


void dlist_append(struct Item *item, struct Item **list, struct Item **last)
{
	if (*last)
		(*last)->next = item;
	else
		*list = item;

	item->prev = *last;
	item->next = NULL;
	*last = item;
}


void read_stdin(void)
{
	char buf[BUFSIZ], *p;
	size_t i, size = 0;
	struct Item *item;

	menu.head = NULL;

	for (i = 0; fgets(buf, sizeof(buf), stdin); i++) {
		if (size <= (i+1) * sizeof(*menu.head)) {
			size += BUFSIZ;
			menu.head = xrealloc(menu.head, size);
		}

		p = strchr(buf, '\n');
		if (p)
			*p = '\0';

		if ((buf[0] == '#') ||
		   (buf[0] == '\n') ||
		   (buf[0] == '\0')) {
			i--;
			continue;
		}

		menu.head[i].t[0] = strdup(buf);
		if (!menu.head[i].t[0])
			die("cannot strdup");

		parse_csv(&menu.head[i]);
	}

	if (!menu.head || i <= 0)
		die("input file contains no menu items");

	/*
	 * Using "menu.head[i].t[0] = NULL" as a dynamic array end-marker
	 * rather than menu.nr_items_in_list or menu.tail.
	 */
	menu.head[i].t[0] = NULL;

	/* menu.nr_items_in_list holds the number of items in the dynamic array */
	/* Not currently using menu.nr_items_in_list in code except to define the cairo buf */
	menu.nr_items_in_list = i;

	/* Create doubly-linked list */
	menu.tail = NULL;
	for (item = menu.head; item && item->t[0]; item++)
		dlist_append(item, &menu.head, &menu.tail);

	/* Populate tag field */
	for (item = menu.head; item && item->t[0]; item++)
		item->tag = parse_caret_action(item->t[1], "^tag(");

}

void run(void)
{
	XEvent ev;
	struct Item *item;
	int oldy, oldx;

	oldx = oldy = 0;

	while (!XNextEvent(ui->dpy, &ev)) {
		switch (ev.type) {
		case ButtonPress:
			mouse_event(&ev);
			break;
		case KeyPress:
			key_event(&ev.xkey);
			draw_menu();
			break;
		case Expose:
			if (ev.xexpose.count == 0)
				ui_map_window(geo_get_menu_width(), geo_get_menu_height());
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(ui->dpy, ui->win);
			break;
		}

		/* Move highlighting with mouse */
		/* Current mouse position is (ev.xbutton.x, ev.xbutton.y) */

		struct Point mouse_coords;

		mouse_coords.x = ev.xbutton.x - geo_get_menu_x0() - MOUSE_FUDGE;
		mouse_coords.y = ev.xbutton.y - geo_get_menu_y0() - MOUSE_FUDGE;

		if ((mouse_coords.x != oldx) || (mouse_coords.y != oldy)) {
			for (item = menu.first; item && item->t[0] && item->prev != menu.last; item++) {
				if (ui_is_point_in_area(mouse_coords, item->area)) {
					if (menu.sel != item) {
						menu.sel = item;
						draw_menu();
						break;
					}
				}
			}
		}
		oldx = mouse_coords.x;
		oldy = mouse_coords.y;
	}
}


void init_geo_variables_from_config(void)
{
	geo_set_menu_margin_x(config.menu_margin_x);
	geo_set_menu_margin_y(config.menu_margin_y);
	geo_set_menu_width(config.menu_width);
	geo_set_item_margin_x(config.item_margin_x);
	geo_set_item_margin_y(config.item_margin_y);
	geo_set_font(config.font);
	geo_set_item_height(config.item_height);
}

int main(int argc, char *argv[])
{
	int i;
	char *config_file = NULL;
	char *checkout_arg = NULL;

	config_set_defaults();
	menu.title = NULL;

	for (i = 1; i < argc; i++)
		if (!strncmp(argv[i], "--config-file=", 14))
			config_file = strdup(argv[i] + 14);
	if (!config_file)
		config_file = strdup("~/.config/jgmenu/jgmenurc");

	if (config_file) {
		if (config_file[0] == '~')
			config_file = expand_tilde(config_file);
		config_parse_file(config_file);
	}

	for (i = 1; i < argc; i++)
		if (!strncmp(argv[i], "--version", 9)) {
			printf("%s\n", VERSION);
			exit(0);
		} else if (!strncmp(argv[i], "--no-spawn", 10)) {
			config.spawn = 0;
		} else if (!strncmp(argv[i], "--debug", 7)) {
			config.debug_mode = 1;
		} else if (!strncmp(argv[i], "--checkout=", 11)) {
			checkout_arg = argv[i] + 11;
		} else if (!strncmp(argv[i], "--fixed-height=", 15)) {
			config.min_items = atoi(argv[i] + 15);
			config.max_items = config.min_items;
		}

	ui_init();
	geo_init();
	init_geo_variables_from_config();

	read_stdin();
	
	if (checkout_arg)
		checkout_submenu(checkout_arg);
	else if (menu.head->tag)
		checkout_submenu(menu.head->tag);
	else
		checkout_submenu(NULL);

	if (config.debug_mode)
		debug_dlist();

	grabkeyboard();
	grabpointer();

	ui_create_window(geo_get_menu_x0(), geo_get_menu_y0(),
			 geo_get_menu_width(), geo_get_menu_height());
	ui_init_canvas(geo_get_menu_width(), geo_get_screen_height());
	ui_init_cairo(geo_get_menu_width(), geo_get_screen_height(), config.font);

	init_menuitem_coordinates();
	draw_menu();

	run();

	ui_cleanup();

	return 0;
}
