#ifndef PS_EVENT_H
#define PS_EVENT_H

#include "ps_vm.h"

void ps_event_init(PSVM *vm);
int ps_event_push(PSVM *vm, const char *type);
int ps_event_push_value(PSVM *vm, PSValue value);

#endif /* PS_EVENT_H */
