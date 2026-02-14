We have 2 FSMs. One for crumb and one for pouch.

Crumb:

In Pouch is the initial state.
If the hall effect sensor is 1, it stays in the pouch.
If the hall effect sensor is 0, we move to the standby state.
If we get a message in the standby state, we need to determine that type of message it is.
If the message is a genaric message, we need to forward it to the next node and return to the stnadby state.
If the message is a pickup message, this means th previous node has been picked up. We need to blink the LED and buzzer, and enter a waiting_for_pickup state.

In waiting_for_pickup state, whenw e see that the hall effect sensor is 1, we move to the in pouch state, and we turn off the blinking led and buzzer. We also need to send a pickup message to the next node.

Pouch:
The initial state sets n_crumbs = 4 and c_id to be A. We move to the not empty state.
In the not empty state, if we detect that the sginal strength of node c_id is below a threshold, we move to a "drop crumb" state.
In the drop cromb state, we actuate a servo, which will drop the crumbn. We then decrement n_crumbs by 1 and update c_id = c_id.next
If the n_crumbs != 0, move to not empty state. If the n_crumbs == 0, move to empty state.. In the empty state, we stay in here indefinitely.
