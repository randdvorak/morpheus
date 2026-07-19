#ifndef MORPHEUS_THEME_H
#define MORPHEUS_THEME_H

static void morph_apply_light_theme(struct nk_context *context)
{
    struct nk_color colors[NK_COLOR_COUNT];
    colors[NK_COLOR_TEXT] = nk_rgba(238, 242, 247, 255);
    colors[NK_COLOR_WINDOW] = nk_rgba(66, 77, 92, 248);
    colors[NK_COLOR_HEADER] = nk_rgba(72, 105, 140, 255);
    colors[NK_COLOR_BORDER] = nk_rgba(116, 134, 153, 255);
    colors[NK_COLOR_BUTTON] = nk_rgba(76, 96, 120, 255);
    colors[NK_COLOR_BUTTON_HOVER] = nk_rgba(75, 125, 165, 255);
    colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(56, 145, 196, 255);
    colors[NK_COLOR_TOGGLE] = nk_rgba(82, 94, 108, 255);
    colors[NK_COLOR_TOGGLE_HOVER] = nk_rgba(96, 112, 130, 255);
    colors[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(56, 189, 248, 255);
    colors[NK_COLOR_SELECT] = nk_rgba(74, 91, 108, 255);
    colors[NK_COLOR_SELECT_ACTIVE] = nk_rgba(14, 165, 233, 255);
    colors[NK_COLOR_SLIDER] = nk_rgba(76, 88, 102, 255);
    colors[NK_COLOR_SLIDER_CURSOR] = nk_rgba(56, 189, 248, 255);
    colors[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(14, 165, 233, 255);
    colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(2, 132, 199, 255);
    colors[NK_COLOR_PROPERTY] = nk_rgba(77, 89, 104, 255);
    colors[NK_COLOR_EDIT] = nk_rgba(52, 62, 75, 255);
    colors[NK_COLOR_EDIT_CURSOR] = nk_rgba(125, 211, 252, 255);
    colors[NK_COLOR_COMBO] = nk_rgba(70, 83, 99, 255);
    colors[NK_COLOR_CHART] = nk_rgba(50, 61, 74, 255);
    colors[NK_COLOR_CHART_COLOR] = nk_rgba(14, 165, 233, 255);
    colors[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba(249, 115, 22, 255);
    colors[NK_COLOR_SCROLLBAR] = nk_rgba(52, 62, 75, 255);
    colors[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(105, 122, 140, 255);
    colors[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(125, 145, 166, 255);
    colors[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(148, 168, 190, 255);
    colors[NK_COLOR_TAB_HEADER] = nk_rgba(72, 105, 140, 255);
    colors[NK_COLOR_KNOB] = colors[NK_COLOR_SLIDER];
    colors[NK_COLOR_KNOB_CURSOR] = colors[NK_COLOR_SLIDER_CURSOR];
    colors[NK_COLOR_KNOB_CURSOR_HOVER] = colors[NK_COLOR_SLIDER_CURSOR_HOVER];
    colors[NK_COLOR_KNOB_CURSOR_ACTIVE] = colors[NK_COLOR_SLIDER_CURSOR_ACTIVE];
    nk_style_from_table(context, colors);
}

#endif
