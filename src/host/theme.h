#ifndef MORPHEUS_THEME_H
#define MORPHEUS_THEME_H

static void morph_apply_light_theme(struct nk_context *context)
{
    struct nk_color colors[NK_COLOR_COUNT];
    colors[NK_COLOR_TEXT] = nk_rgba(30, 41, 59, 255);
    colors[NK_COLOR_WINDOW] = nk_rgba(248, 250, 252, 248);
    colors[NK_COLOR_HEADER] = nk_rgba(191, 219, 254, 255);
    colors[NK_COLOR_BORDER] = nk_rgba(148, 163, 184, 255);
    colors[NK_COLOR_BUTTON] = nk_rgba(219, 234, 254, 255);
    colors[NK_COLOR_BUTTON_HOVER] = nk_rgba(186, 230, 253, 255);
    colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(147, 197, 253, 255);
    colors[NK_COLOR_TOGGLE] = nk_rgba(226, 232, 240, 255);
    colors[NK_COLOR_TOGGLE_HOVER] = nk_rgba(203, 213, 225, 255);
    colors[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(59, 130, 246, 255);
    colors[NK_COLOR_SELECT] = nk_rgba(224, 242, 254, 255);
    colors[NK_COLOR_SELECT_ACTIVE] = nk_rgba(125, 211, 252, 255);
    colors[NK_COLOR_SLIDER] = nk_rgba(226, 232, 240, 255);
    colors[NK_COLOR_SLIDER_CURSOR] = nk_rgba(96, 165, 250, 255);
    colors[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(59, 130, 246, 255);
    colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(37, 99, 235, 255);
    colors[NK_COLOR_PROPERTY] = nk_rgba(241, 245, 249, 255);
    colors[NK_COLOR_EDIT] = nk_rgba(255, 255, 255, 255);
    colors[NK_COLOR_EDIT_CURSOR] = nk_rgba(37, 99, 235, 255);
    colors[NK_COLOR_COMBO] = nk_rgba(241, 245, 249, 255);
    colors[NK_COLOR_CHART] = nk_rgba(255, 255, 255, 255);
    colors[NK_COLOR_CHART_COLOR] = nk_rgba(14, 165, 233, 255);
    colors[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba(249, 115, 22, 255);
    colors[NK_COLOR_SCROLLBAR] = nk_rgba(226, 232, 240, 255);
    colors[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(148, 163, 184, 255);
    colors[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(100, 116, 139, 255);
    colors[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(71, 85, 105, 255);
    colors[NK_COLOR_TAB_HEADER] = nk_rgba(219, 234, 254, 255);
    colors[NK_COLOR_KNOB] = colors[NK_COLOR_SLIDER];
    colors[NK_COLOR_KNOB_CURSOR] = colors[NK_COLOR_SLIDER_CURSOR];
    colors[NK_COLOR_KNOB_CURSOR_HOVER] = colors[NK_COLOR_SLIDER_CURSOR_HOVER];
    colors[NK_COLOR_KNOB_CURSOR_ACTIVE] = colors[NK_COLOR_SLIDER_CURSOR_ACTIVE];
    nk_style_from_table(context, colors);
}

#endif
