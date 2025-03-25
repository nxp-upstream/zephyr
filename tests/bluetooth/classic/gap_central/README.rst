# Bluetooth GAP Central Role Test Specification

## Overview

This document contains a comprehensive test suite for verifying the functionality of Bluetooth GAP Central Role implementations. The test cases cover device discovery (general and limited inquiry), connection establishment, and disconnection scenarios.

## Table of Contents

- [GAP-C-1: General Inquiry with Successful Connection and Active Disconnection](#gapc1)
- [GAP-C-2: General Inquiry with Connection and Passive Disconnection](#gapc2)
- [GAP-C-3: General Inquiry with Connection Rejection](#gapc3)
- [GAP-C-4: Limited Inquiry with Successful Connection and Active Disconnection](#gapc4)
- [GAP-C-5: Limited Inquiry with Connection and Passive Disconnection](#gapc5)
- [GAP-C-6: Limited Inquiry with Connection Rejection](#gapc6)

---

<a id='gapc1'></a>
## Case.1: General Inquiry with Successful Connection and Active Disconnection

**Case ID:** GAP-C-1

### Purpose

Verify that a GAP Central can perform general inquiry, discover a device, successfully connect and actively disconnect

### Covered Features

- BR/EDR
- Initiation of general inquiry
- Initiation of device discovery
- Link Establishment as initiator
- Connection Establishment as initiator
- Central

### Test Conditions

- DUT configured as GAP Central role
- Peripheral device in discoverable mode
- Both devices are within range

### Test Procedure

1. Start the DUT in GAP Central role
2. Initiate general inquiry
3. Wait for device discovery
4. Send connection request to discovered device
5. Verify connected event is received
6. Send disconnection request
7. Wait for disconnection event

### Expected Outcomes

1. General inquiry successfully initiated
2. Device is discovered during inquiry
3. Connection request is successfully sent
4. Connected event is received
5. Disconnection request is sent successfully
6. Disconnection event is received

---

<a id='gapc2'></a>
## Case.2: General Inquiry with Connection and Passive Disconnection

**Case ID:** GAP-C-2

### Purpose

Verify that a GAP Central can perform general inquiry, discover a device, connect and handle passive disconnection

### Covered Features

- BR/EDR
- Initiation of general inquiry
- Initiation of device discovery
- Link Establishment as initiator
- Connection Establishment as initiator
- Central

### Test Conditions

- DUT configured as GAP Central role
- Peripheral device in discoverable mode
- Both devices are within range

### Test Procedure

1. Start the DUT in GAP Central role
2. Initiate general inquiry
3. Wait for device discovery
4. Send connection request to discovered device
5. Verify connected event is received
6. Wait for disconnection event (initiated by peripheral or due to timeout/link loss)

### Expected Outcomes

1. General inquiry successfully initiated
2. Device is discovered during inquiry
3. Connection request is successfully sent
4. Connected event is received
5. Disconnection event is received when connection is terminated externally

---

<a id='gapc3'></a>
## Case.3: General Inquiry with Connection Rejection

**Case ID:** GAP-C-3

### Purpose

Verify that a GAP Central can handle connection rejection after general inquiry

### Covered Features

- BR/EDR
- Initiation of general inquiry
- Initiation of device discovery
- Connection Establishment as initiator
- Central

### Test Conditions

- DUT configured as GAP Central role
- Peripheral device in discoverable mode but configured to reject connections
- Both devices are within range

### Test Procedure

1. Start the DUT in GAP Central role
2. Initiate general inquiry
3. Wait for device discovery
4. Send connection request to discovered device
5. Observe connection rejection

### Expected Outcomes

1. General inquiry successfully initiated
2. Device is discovered during inquiry
3. Connection request is sent
4. Connection request is rejected by peripheral
5. Central properly handles the rejection

---

<a id='gapc4'></a>
## Case.4: Limited Inquiry with Successful Connection and Active Disconnection

**Case ID:** GAP-C-4

### Purpose

Verify that a GAP Central can perform limited inquiry, discover a device, successfully connect and actively disconnect

### Covered Features

- BR/EDR
- Initiation of limited inquiry
- Initiation of device discovery
- Link Establishment as initiator
- Connection Establishment as initiator
- Central

### Test Conditions

- DUT configured as GAP Central role
- Peripheral device in limited discoverable mode
- Both devices are within range

### Test Procedure

1. Start the DUT in GAP Central role
2. Initiate limited inquiry
3. Wait for device discovery
4. Send connection request to discovered device
5. Verify connected event is received
6. Send disconnection request
7. Wait for disconnection event

### Expected Outcomes

1. Limited inquiry successfully initiated
2. Device in limited discoverable mode is discovered
3. Connection request is successfully sent
4. Connected event is received
5. Disconnection request is sent successfully
6. Disconnection event is received

---

<a id='gapc5'></a>
## Case.5: Limited Inquiry with Connection and Passive Disconnection

**Case ID:** GAP-C-5

### Purpose

Verify that a GAP Central can perform limited inquiry, discover a device, connect and handle passive disconnection

### Covered Features

- BR/EDR
- Initiation of limited inquiry
- Initiation of device discovery
- Link Establishment as initiator
- Connection Establishment as initiator
- Central

### Test Conditions

- DUT configured as GAP Central role
- Peripheral device in limited discoverable mode
- Both devices are within range

### Test Procedure

1. Start the DUT in GAP Central role
2. Initiate limited inquiry
3. Wait for device discovery
4. Send connection request to discovered device
5. Verify connected event is received
6. Wait for disconnection event (initiated by peripheral or due to timeout/link loss)

### Expected Outcomes

1. Limited inquiry successfully initiated
2. Device in limited discoverable mode is discovered
3. Connection request is successfully sent
4. Connected event is received
5. Disconnection event is received when connection is terminated externally

---

<a id='gapc6'></a>
## Case.6: Limited Inquiry with Connection Rejection

**Case ID:** GAP-C-6

### Purpose

Verify that a GAP Central can handle connection rejection after limited inquiry

### Covered Features

- BR/EDR
- Initiation of limited inquiry
- Initiation of device discovery
- Connection Establishment as initiator
- Central

### Test Conditions

- DUT configured as GAP Central role
- Peripheral device in limited discoverable mode but configured to reject connections
- Both devices are within range

### Test Procedure

1. Start the DUT in GAP Central role
2. Initiate limited inquiry
3. Wait for device discovery
4. Send connection request to discovered device
5. Observe connection rejection

### Expected Outcomes

1. Limited inquiry successfully initiated
2. Device in limited discoverable mode is discovered
3. Connection request is sent
4. Connection request is rejected by peripheral
5. Central properly handles the rejection

---

