/* ui.c — Éléments d'interface. */
#include "ui.h"
#include "gfx.h"

void ui_title_bar(char const *title)
{
    gfx_rect(0, 0, GFX_WIDTH - 1, 8, GFX_BLACK);
    gfx_text_at(GFX_WIDTH / 2, 1, title, GFX_WHITE, ALIGN_CENTER);
}

void ui_items(int x1, int x2, int y, char const *const *items, int count,
    int selected)
{
    for (int i = 0; i < count; i++, y += UI_ITEM_HEIGHT) {
        gfx_text(x1 + 3, y, items[i], GFX_BLACK);
        if (i == selected)
            gfx_rect(x1, y - 1, x2, y + FONT_HEIGHT, GFX_INVERT);
    }
}

ui_action_t ui_list_input(input_t key, int *selected, int count)
{
    switch (key) {
    case IN_UP:
        *selected = (*selected + count - 1) % count;
        return UI_NONE;
    case IN_DOWN:
        *selected = (*selected + 1) % count;
        return UI_NONE;
    case IN_EXE:
    case IN_SHIFT:
        return UI_CHOOSE;
    case IN_EXIT:
        return UI_CANCEL;
    default:
        return UI_NONE;
    }
}

/* Plus grande largeur parmi des textes. */
static int max_width(char const *const *texts, int count)
{
    int width = 0;
    for (int i = 0; i < count; i++) {
        int w = gfx_text_width(texts[i]);
        if (w > width)
            width = w;
    }
    return width;
}

int ui_dialog(char const *title, char const *const *lines, int line_count,
    char const *const *items, int item_count, int selected,
    ui_background_t background, void const *context)
{
    /* Dimensions de la boîte d'après son contenu. */
    int width = gfx_text_width(title);
    int w = max_width(lines, line_count);
    if (w > width) width = w;
    w = max_width(items, item_count) + 6;
    if (w > width) width = w;
    width += 10;
    if (width < 84) width = 84;
    if (width > GFX_WIDTH - 2) width = GFX_WIDTH - 2;

    int height = 2 + FONT_HEIGHT + 4 + item_count * UI_ITEM_HEIGHT + 2;
    if (line_count > 0)
        height += line_count * (FONT_HEIGHT + 1) + 2;

    int x1 = (GFX_WIDTH - width) / 2, x2 = x1 + width - 1;
    int y1 = (GFX_HEIGHT - height) / 2, y2 = y1 + height - 1;

    for (;;) {
        gfx_clear();
        if (background)
            background(context);

        /* Ombre, fond et contour. */
        gfx_rect(x1 + 1, y1 + 1, x2 + 1, y2 + 1, GFX_BLACK);
        gfx_rect(x1, y1, x2, y2, GFX_WHITE);
        gfx_frame(x1, y1, x2, y2, GFX_BLACK);

        int y = y1 + 2;
        gfx_text_at((x1 + x2) / 2, y, title, GFX_BLACK, ALIGN_CENTER);
        y += FONT_HEIGHT + 2;
        gfx_rect(x1 + 2, y, x2 - 2, y, GFX_BLACK);
        y += 2;

        for (int i = 0; i < line_count; i++, y += FONT_HEIGHT + 1)
            gfx_text_at((x1 + x2) / 2, y, lines[i], GFX_BLACK, ALIGN_CENTER);
        if (line_count > 0)
            y += 2;

        ui_items(x1 + 2, x2 - 2, y + 1, items, item_count, selected);
        gfx_present();

        input_t key = pf_getkey(false);
        switch (ui_list_input(key, &selected, item_count)) {
        case UI_CHOOSE:
            return selected;
        case UI_CANCEL:
            return -1;
        default:
            break;
        }
    }
}

bool ui_confirm(char const *title, char const *line1, char const *line2,
    char const *yes, char const *no, ui_background_t background,
    void const *context)
{
    char const *lines[] = { line1, line2 };
    char const *items[] = { yes, no };
    /* « Non » est présélectionné : il faut un choix délibéré pour accepter. */
    return ui_dialog(title, lines, line2 ? 2 : 1, items, 2, 1, background,
        context) == 0;
}
