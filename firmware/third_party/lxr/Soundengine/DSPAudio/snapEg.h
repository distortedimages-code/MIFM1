/*
 * snapEg.h
 *
 *  Created on: 16.06.2013
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 *
 * Modified for MIFM1 (2026): ported to the FM-1, see MIFM1.md in this directory.
 */


#ifndef SNAPEG_H_
#define SNAPEG_H_

#include "lxr_port.h"

typedef struct Snap_EG_Struct
{
	float value;
} SnapEg;


void SnapEg_init(SnapEg* eg);
void SnapEg_trigger(SnapEg* eg);
float SnapEg_calc(SnapEg* eg, float time);



#endif /* SNAPEG_H_ */
