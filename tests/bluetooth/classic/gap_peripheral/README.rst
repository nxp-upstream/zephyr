# Bluetooth GAP Peripheral Test Specification

## Overview

This document defines a comprehensive set of test cases for verifying the functionality of Bluetooth Low Energy Generic Access Profile (GAP) peripheral devices. The test cases cover different discoverable and connectable mode combinations, connection establishment, and disconnection procedures.

## Table of Contents

- [Case.1: Non-discoverable and Non-connectable Mode Verification](#gapp1)
- [Case.2: Rejecting Connection Requests in Non-discoverable Mode](#gapp2)
- [Case.3: Central Device Disconnection in Non-discoverable Mode](#gapp3)
- [Case.4: Peripheral-Initiated Disconnection in Non-discoverable Mode](#gapp4)
- [Case.5: Limited Discoverable and Non-connectable Mode Verification](#gapp5)
- [Case.6: Rejecting Connection Requests in Limited Discoverable Mode](#gapp6)
- [Case.7: Central Device Disconnection in Limited Discoverable Mode](#gapp7)
- [Case.8: Peripheral-Initiated Disconnection in Limited Discoverable Mode](#gapp8)
- [Case.9: General Discoverable and Non-connectable Mode Verification](#gapp9)
- [Case.10: Rejecting Connection Requests in General Discoverable Mode](#gapp10)
- [Case.11: Central Device Disconnection in General Discoverable Mode](#gapp11)
- [Case.12: Peripheral-Initiated Disconnection in General Discoverable Mode](#gapp12)

---

<a id='gapp1'></a>
## Case.1: Non-discoverable and Non-connectable Mode Verification

**Case ID:** GAP-P-1

### Purpose

Verify that a peripheral device in non-discoverable and non-connectable mode cannot be discovered and connected by a central device

### Covered Features

- Non-discoverable mode
- Non-connectable mode
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning test

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to non-discoverable mode
3. Configure the peripheral device to non-connectable mode
4. Use the central device to perform scanning operation
5. End of procedure

### Expected Outcomes

1. The central device cannot discover the peripheral device
2. The peripheral device enters the end state directly after completing non-discoverable and non-connectable configuration
3. The peripheral device does not respond to any connection requests

---

<a id='gapp2'></a>
## Case.2: Rejecting Connection Requests in Non-discoverable Mode

**Case ID:** GAP-P-2

### Purpose

Verify that a peripheral device in non-discoverable mode can properly reject connection requests that do not meet specified conditions

### Covered Features

- Non-discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for connection test
- The central device knows the peripheral device's address in advance
- The peripheral device is configured to reject specific connection parameters

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to non-discoverable mode
3. Configure the peripheral device to connectable mode
4. Set the peripheral device to reject connection requests with specific parameters
5. Use the central device to initiate a connection request with non-compliant parameters via known address
6. The peripheral device waits for connection request
7. The peripheral device rejects the connection request
8. End of procedure

### Expected Outcomes

1. The central device cannot discover the peripheral device by scanning
2. The central device sends connection request via known address
3. The peripheral device correctly receives and rejects the connection request
4. The central device receives the rejection response
5. The peripheral device successfully enters the end state

---

<a id='gapp3'></a>
## Case.3: Central Device Disconnection in Non-discoverable Mode

**Case ID:** GAP-P-3

### Purpose

Verify that a peripheral device in non-discoverable mode can properly handle a disconnection request initiated by the central device

### Covered Features

- Non-discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Link Establishment as acceptor
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for connection test
- The central device knows the peripheral device's address in advance

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to non-discoverable mode
3. Configure the peripheral device to connectable mode
4. Verify peripheral device cannot be discovered
5. Use the central device to initiate a connection request via known address
6. The peripheral device waits for connection request
7. The peripheral device accepts the connection request
8. The peripheral device waits for connection event
9. The central device sends a disconnection request
10. The peripheral device waits for disconnection event
11. End of procedure

### Expected Outcomes

1. The central device successfully connects to the peripheral device using known address
2. The connection is established successfully and data can be exchanged
3. The peripheral device properly handles the disconnection request initiated by the central device
4. The connection is properly terminated
5. The peripheral device successfully enters the end state

---

<a id='gapp4'></a>
## Case.4: Peripheral-Initiated Disconnection in Non-discoverable Mode

**Case ID:** GAP-P-4

### Purpose

Verify that a peripheral device in non-discoverable mode can initiate and properly handle a disconnection procedure

### Covered Features

- Non-discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Link Establishment as acceptor
- Peripheral
- Channel Establishment as initiator

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for connection test
- The central device knows the peripheral device's address in advance

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to non-discoverable mode
3. Configure the peripheral device to connectable mode
4. Verify peripheral device cannot be discovered
5. Use the central device to initiate a connection request via known address
6. The peripheral device waits for connection request
7. The peripheral device accepts the connection request
8. The peripheral device waits for connection event
9. The peripheral device sends a disconnection request
10. The peripheral device waits for disconnection event
11. End of procedure

### Expected Outcomes

1. The central device successfully connects to the peripheral device using known address
2. The connection is established successfully and data can be exchanged
3. The peripheral device successfully initiates a disconnection request
4. The connection is properly terminated
5. The peripheral device successfully enters the end state

---

<a id='gapp5'></a>
## Case.5: Limited Discoverable and Non-connectable Mode Verification

**Case ID:** GAP-P-5

### Purpose

Verify that a peripheral device in limited discoverable but non-connectable mode can be discovered but not connected, and that discoverability expires after a certain time

### Covered Features

- Limited discoverable mode
- Non-connectable mode
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning test
- Limited discoverable mode time is set

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to limited discoverable mode
3. Configure the peripheral device to non-connectable mode
4. Verify peripheral device can not be discovered in general inquiry
5. Verify peripheral device can be discovered in limited inquiry
6. Use the central device to attempt connection to the discovered device
7. Wait to allow limited discoverable mode to expire
8. Use the central device to perform scanning operation again
9. End of procedure

### Expected Outcomes

1. In the first scan, the central device can discover the peripheral device
2. When the central device attempts to connect, the connection request fails
3. In the second scan, when limited discoverable mode has expired, the central device cannot discover the peripheral device
4. The peripheral device enters the end state directly after completing non-connectable configuration

---

<a id='gapp6'></a>
## Case.6: Rejecting Connection Requests in Limited Discoverable Mode

**Case ID:** GAP-P-6

### Purpose

Verify that a peripheral device in limited discoverable mode can properly reject connection requests that do not meet specified conditions

### Covered Features

- Limited discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning and connection tests
- Limited discoverable mode time is set to
- The peripheral device is configured to reject specific connection parameters

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to limited discoverable mode
3. Configure the peripheral device to connectable mode
4. Set the peripheral device to reject connection requests with specific parameters
5. Use the central device to perform scanning operation
6. Use the central device to initiate a connection request with non-compliant parameters
7. The peripheral device waits for connection request
8. The peripheral device rejects the connection request
9. End of procedure

### Expected Outcomes

1. The central device can discover the peripheral device
2. The peripheral device correctly receives the connection request
3. The peripheral device identifies non-compliant connection parameters
4. The peripheral device successfully rejects the connection request
5. The central device receives the rejection response
6. The peripheral device successfully enters the end state

---

<a id='gapp7'></a>
## Case.7: Central Device Disconnection in Limited Discoverable Mode

**Case ID:** GAP-P-7

### Purpose

Verify that a peripheral device in limited discoverable mode can properly handle a disconnection request initiated by the central device

### Covered Features

- Limited discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Link Establishment as acceptor
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning and connection tests
- Limited discoverable mode time is set

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to limited discoverable mode
3. Configure the peripheral device to connectable mode
4. Use the central device to perform scanning operation
5. Use the central device to initiate a connection request
6. The peripheral device waits for connection request
7. The peripheral device accepts the connection request
8. The peripheral device waits for connection event
9. The central device sends a disconnection request
10. The peripheral device waits for disconnection event
11. End of procedure

### Expected Outcomes

1. The central device can discover and connect to the peripheral device
2. The connection is established successfully and data can be exchanged
3. The peripheral device properly handles the disconnection request initiated by the central device
4. The connection is properly terminated
5. The peripheral device successfully enters the end state

---

<a id='gapp8'></a>
## Case.8: Peripheral-Initiated Disconnection in Limited Discoverable Mode

**Case ID:** GAP-P-8

### Purpose

Verify that a peripheral device in limited discoverable mode can initiate and properly handle a disconnection procedure

### Covered Features

- Limited discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Link Establishment as acceptor
- Channel Establishment as initiator
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning and connection tests
- Limited discoverable mode time is set

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to limited discoverable mode
3. Configure the peripheral device to connectable mode
4. Use the central device to perform scanning operation
5. Use the central device to initiate a connection request
6. The peripheral device waits for connection request
7. The peripheral device accepts the connection request
8. The peripheral device waits for connection event
9. The peripheral device sends a disconnection request
10. The peripheral device waits for disconnection event
11. End of procedure

### Expected Outcomes

1. The central device can discover and connect to the peripheral device
2. The connection is established successfully and data can be exchanged
3. The peripheral device successfully initiates a disconnection request
4. The connection is properly terminated
5. The peripheral device successfully enters the end state

---

<a id='gapp9'></a>
## Case.9: General Discoverable and Non-connectable Mode Verification

**Case ID:** GAP-P-9

### Purpose

Verify that a peripheral device in general discoverable but non-connectable mode can be continuously discovered but not connected

### Covered Features

- General discoverable mode
- Non-connectable mode
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning test

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to general discoverable mode
3. Configure the peripheral device to non-connectable mode
4. Use the central device to perform scanning operation
5. Use the central device to attempt connection to the discovered device
6. Wait for 2 minutes
7. Use the central device to perform scanning operation again
8. End of procedure

### Expected Outcomes

1. The central device can discover the peripheral device (in both scans, as it is in general discoverable mode)
2. When the central device attempts to connect, the connection request fails
3. The peripheral device enters the end state directly after completing non-connectable configuration
4. General discoverable mode does not time out, and the device remains discoverable

---

<a id='gapp10'></a>
## Case.10: Rejecting Connection Requests in General Discoverable Mode

**Case ID:** GAP-P-10

### Purpose

Verify that a peripheral device in general discoverable mode can properly reject connection requests that do not meet specified conditions

### Covered Features

- General discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning and connection tests
- The peripheral device is configured to reject specific connection parameters

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to general discoverable mode
3. Configure the peripheral device to connectable mode
4. Set the peripheral device to reject connection requests with specific parameters
5. Use the central device to perform scanning operation
6. Use the central device to initiate a connection request with non-compliant parameters
7. The peripheral device waits for connection request
8. The peripheral device rejects the connection request
9. End of procedure

### Expected Outcomes

1. The central device can discover the peripheral device
2. The peripheral device correctly receives the connection request
3. The peripheral device identifies non-compliant connection parameters
4. The peripheral device successfully rejects the connection request
5. The central device receives the rejection response
6. The peripheral device successfully enters the end state

---

<a id='gapp11'></a>
## Case.11: Central Device Disconnection in General Discoverable Mode

**Case ID:** GAP-P-11

### Purpose

Verify that a peripheral device in general discoverable mode can properly handle a disconnection request initiated by the central device

### Covered Features

- General discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Link Establishment as acceptor
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning and connection tests

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to general discoverable mode
3. Configure the peripheral device to connectable mode
4. Use the central device to perform scanning operation
5. Use the central device to initiate a connection request
6. The peripheral device waits for connection request
7. The peripheral device accepts the connection request
8. The peripheral device waits for connection event
9. The central device sends a disconnection request
10. The peripheral device waits for disconnection event
11. End of procedure

### Expected Outcomes

1. The central device can discover and connect to the peripheral device
2. The connection is established successfully and data can be exchanged
3. The peripheral device properly handles the disconnection request initiated by the central device
4. The connection is properly terminated
5. The peripheral device successfully enters the end state

---

<a id='gapp12'></a>
## Case.12: Peripheral-Initiated Disconnection in General Discoverable Mode

**Case ID:** GAP-P-12

### Purpose

Verify that a peripheral device in general discoverable mode can initiate and properly handle a disconnection procedure

### Covered Features

- General discoverable mode
- Connectable mode
- Connection Establishment as acceptor
- Link Establishment as acceptor
- Channel Establishment as initiator
- Peripheral

### Test Conditions

- Test device is configured as GAP Peripheral role
- A central device is available for scanning and connection tests

### Test Procedure

1. Start the peripheral device
2. Configure the peripheral device to general discoverable mode
3. Configure the peripheral device to connectable mode
4. Use the central device to perform scanning operation
5. Use the central device to initiate a connection request
6. The peripheral device waits for connection request
7. The peripheral device accepts the connection request
8. The peripheral device waits for connection event
9. The peripheral device sends a disconnection request
10. The peripheral device waits for disconnection event
11. End of procedure

### Expected Outcomes

1. The central device can discover and connect to the peripheral device
2. The connection is established successfully and data can be exchanged
3. The peripheral device successfully initiates a disconnection request
4. The connection is properly terminated
5. The peripheral device successfully enters the end state

---

