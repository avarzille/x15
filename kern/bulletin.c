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
 */

#include <stdbool.h>

#include <kern/bulletin.h>
#include <kern/list.h>

void
bulletin_subscriber_init(struct bulletin_subscriber *subscriber,
                         bulletin_notif_fn_t fn, void *arg)
{
    subscriber->notif_fn = fn;
    subscriber->arg = arg;
}

static void
bulletin_subscriber_notify(const struct bulletin_subscriber *subscriber)
{
    subscriber->notif_fn(subscriber->arg);
}

void
bulletin_subscribe(struct bulletin *bulletin,
                   struct bulletin_subscriber *subscriber)
{
    if (bulletin->published) {
        bulletin_subscriber_notify(subscriber);
    } else {
        list_insert_tail(&bulletin->subscribers, &subscriber->node);
    }
}

void
bulletin_publish(struct bulletin *bulletin)
{
    struct bulletin_subscriber *subscriber;
    struct list *node;

    while (!list_empty(&bulletin->subscribers)) {
        node = list_first(&bulletin->subscribers);
        subscriber = list_entry(node, struct bulletin_subscriber, node);
        list_remove(&subscriber->node);
        bulletin_subscriber_notify(subscriber);
    }

    bulletin->published = true;
}
