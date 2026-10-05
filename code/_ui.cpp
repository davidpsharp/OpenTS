/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "_ui.h"

#include "ui/rml/rmlfile.h"
#include "ui/rml/rmlrender.h"
#include "ui/rml/rmlsystem.h"
#include "ui/uienginehost.h"
#include "ui/uishell.h"

#ifndef _WIN32
#include "rmlrendersoft.h"
#endif

#include <memory>


UIShellClass UIShell(UI_Engine_Host(),
					 std::make_unique<UIRmlSystemClass>(UI_Engine_Host()),
					 std::make_unique<UIRmlFileClass>(),
#ifdef _WIN32
					 std::make_unique<UIRmlBgfxRenderClass>());
#else
					 std::make_unique<UIRmlSoftRenderClass>());
#endif
