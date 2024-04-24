Clock Management API Test
#########################

This test is designed to verify the functionality of hardware clock trees
implementing the clock management API. It defines two dummy devices, which
will both be clock consumers.

Depending on which features are enabled, the following tests will run:

When ``CONFIG_CLOCK_MGMT=y``, apply 3 clock states. Devices should
define these states to exercise as many clock node drivers as possible. One
example might be clocking from a PLL in the default state, a high speed internal
oscillator in the sleep state, and a low speed external oscillator in the
test state. The test will apply the 3 states as follows:

* Verify that each consumer can apply the clock state CLOCK_MGMT_STATE_DEFAULT,
  and that the queried rates match the property "default-clock-rate" for each
  device.

* Verify that each consumer can apply the clock state CLOCK_MGMT_STATE_SLEEP,
  and that the queried rates match the property "sleep-clock-rate" for each
  device.

* Verify that each consumer can apply the clock state CLOCK_MGMT_STATE_TEST,
  and that the queried rates match the property "test-clock-rate" for each
  device.

When ``CONFIG_CLOCK_MGMT_NOTIFY=y``:

* Apply the clock state CLOCK_MGMT_STATE_NOTIFY for each consumer. The
  device specific dt overlay for this test should define this state for each
  dummy device such that applying the state will trigger a notification for
  each device (that is, a root clock shared by both device's clock outputs
  should be reconfigured)

When ``CONFIG_CLOCK_MGMT_SET_RATE=y``:

* Apply the clock state CLOCK_MGMT_STATE_SETRATE for each consumer. Then,
  verify that the frequency for each consumer matches the property
  "setrate-clock-rate".

* Apply the clock state CLOCK_MGMT_STATE_SETRATE1 for each consumer. Then,
  verify that the frequency for each consumer matches the property
  "setrate1-clock-rate".
