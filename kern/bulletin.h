/*
 * Copyright (c) 2017 Richard Braun.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Minimalist publish-subscribe mechanism.
 *
 * The main purpose of bulletins is to allow modules to subscribe to events
 * early during initialization, so that they're notified when dependencies
 * are available. As such, they must have as few dependencies as possible
 * themselves, and must support subscriptions at any time.
 *
 * In addition, because of the requirement for least dependencies, this
 * implementation doesn't provide thread-safety.
 */

#ifndef _KERN_BULLETIN_H
#define _KERN_BULLETIN_H

#include <stdbool.h>

#include <kern/list.h>
#include <kern/macros.h>

/*
 * Type for bulletin notification functions.
 */
typedef void (*bulletin_notif_fn_t)(void *arg);

#include <kern/bulletin_i.h>

/*
 * Statically declare a bulletin.
 *
 * A strong requirement of this module is to provide the ability to subscribe
 * to bulletins at any time. In order to achieve this, users must be able to
 * statically initialize bulletins.
 */
#define BULLETIN_DECLARE(name)                      \
    struct bulletin name = {                        \
        LIST_INITIALIZER((name).subscribers),       \
        false,                                      \
        QUOTE(name),                                \
    }

/*
 * Bulletin subscriber.
 */
struct bulletin_subscriber;

/*
 * Initialize a bulletin subscriber.
 *
 * The notification function is called on publish.
 */
void bulletin_subscriber_init(struct bulletin_subscriber *subscriber,
                              bulletin_notif_fn_t notif_fn, void *arg);

/*
 * Subscribe to a bulletin.
 *
 * The given subscriber must be initialized before calling this function.
 *
 * If the bulletin has already been published, the subscriber is immediately
 * notified.
 */
void bulletin_subscribe(struct bulletin *bulletin,
                        struct bulletin_subscriber *subscriber);

/*
 * Publish a bulletin.
 *
 * All subscribers are notified. Future subscribers are notified as soon as
 * they subscribe.
 *
 * A bulletin may only be published once.
 */
void bulletin_publish(struct bulletin *bulletin);

#endif /* _KERN_BULLETIN_H */
