#ifndef __SUBMISSION_H
#define __SUBMISSION_H

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "eek/eek-types.h"

struct submission;

struct submission* get_submission(struct zwp_input_method_manager_v2 *immanager,
                                  struct zwp_virtual_keyboard_manager_v1 *vkmanager,
                                  struct wl_seat *seat,
                                  LayoutHolder *state);

// Defined in Rust
struct submission* submission_new(struct zwp_input_method_v2 *im, struct zwp_virtual_keyboard_v1 *vk, LayoutHolder *state);
void submission_set_ui(struct submission *self, ServerContextService *ui_context);
void submission_set_keyboard(struct submission *self, LevelKeyboard *keyboard);
#endif
