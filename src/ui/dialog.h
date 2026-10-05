/* dialog.h - small modal confirm dialog (confirm = Yes, back = No). */
#ifndef NOCT_DIALOG_H
#define NOCT_DIALOG_H

#include "ui/hintbar.h"
#include "ui/input.h"

typedef enum { DIALOG_PENDING, DIALOG_YES, DIALOG_NO } dialog_result_t;

void dialog_open_confirm(const char *title, const char *message);
int dialog_is_open(void);
/* Feed input while open. Returns DIALOG_PENDING until answered; closes itself when answered. */
dialog_result_t dialog_update(const input_t *in);
void dialog_draw(void);
/* Hint table to show while the dialog is open. */
int dialog_hints(hint_t *out, int max);

#endif
