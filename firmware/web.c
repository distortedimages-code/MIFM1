/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1's own exports in the browser emulator (app.h APP_WEB; fm1-base's web/emu/emu_web.c includes this file): the
 * FM-1's load as the engine's model has it (mi/engine.cc PLAT_WEB, mi/web_costs.h), for the line under the panel
 * (web/emu/app_worklet.js sends it to the page). i: 0 core 0, 1 core 1, 2 the worst total of the last second, 3 the
 * preparation; tenths of % of the block */
__attribute__((used, visibility("default"))) uint32_t web_fm1_load(uint32_t i)
{
    return i == 0 ? eng_load(0) : i == 1 ? eng_load(1) : i == 2 ? eng_peak() : eng_prep_load();
}

/* the page's "Essayer": kind 0 a model (Plaits', an FM drum's: its kit with it), 1 a filter (its model), on the track
 * playing, its page shown, as ALGORITHM would */
__attribute__((used, visibility("default"))) void web_try(uint32_t kind, uint32_t i)
{
    int t = ui.track;
    if (kind == 0 && i < ENG_MODELS) {
        eng_set_model(t, (int)i);
        go_page(PG_TRACK(t, 0));
    } else if (kind == 1 && i < MF_MODELS) {
        eng_project()->algo[PG_FILT(t, 0)] = (uint8_t)i;
        go_page(VP_FLT1(t));
    } else {
        return;
    }
    dirty = 1;
    ui.full = ui.body_panel = 1;
    ui.anim = 1;                         /* (its name written again) */
    ui.anim_at = plat_ms();
}
