Clock Management API Test
#########################

This test is designed to verify the functionality of the clock management API.
It defines two dummy devices, which will both be clock consumers. In addition,
it defines several dummy clock nodes to verify API functionality.

Depending on which features are enabled, the following tests will run:

When ``CONFIG_CLOCK_MGMT=y``:

* Verify that each consumer can apply the clock state CLOCK_MGMT_STATE_DEFAULT,
  and that the queried rates match the expected value

When ``CONFIG_CLOCK_MGMT_NOTIFY=y``:

* Verify that both consumers receive a notification when one reconfigures
  a shared clock root node

When ``CONFIG_CLOCK_MGMT_SET_RATE=y``:

* Verify that the first consumer to request a rate in the system will
  be able to use any clock nodes in its tree to achieve that rate

* Verify that the first consumer now holds a lock on those clock nodes by
  requesting a rate for the second consumer that would be best satisfied by the
  nodes the first consumer already has a lock on, and making sure that
  alternate nodes are selected

* Verify the first consumer is able to exercise its lock by attempting
  to reconfigure its clock to a new rate. This rate will be selected such
  that the multiplexer node will select a new input source

* Verify that the input source the multiplexer is no longer using was
  unlocked correctly by requesting a frequency for the second consumer
  that would best be satisfied by that input, and verifying that the
  consumer can now achieve that frequency
