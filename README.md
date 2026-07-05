# Router Dataplane Implementation

## 📖 Overview
Repository for the first homework of the Communication Networks class. In this homework, the students implement the dataplane of a network router.


## 📦 Project Architecture & Implementation

### 🗺️ Initialization & Memory Allocation
* **Table Allocation**: At startup, the router allocates memory for the static ARP and MAC tables. It initializes the length of these tables and populates the routing tree by adding each entry from the input routing table.
* **Dynamic ARP Setup**: The initial packet queue is initialized, and the operational length of the dynamic MAC table is set to `0` to prepare for dynamic address resolution.


### 🔄 The Forwarding Process
For each incoming network packet, the router executes the following execution flow:
1.  **Header Extraction**: Extracts the baseline Ethernet and IPv4 headers from the raw packet buffer.
2.  **Checksum Validation**: Verifies if the checksum of the IP header is valid. If the checksum is invalid, the router instantly **drops** the packet.
3.  **Local Destination Check**: Checks if the packet is destined for the router itself. If true, it will later pass it to the ICMP handler to be processed as an `echo_reply`.
4.  **Route Lookup**: Searches for the optimal next hop using the **Longest Prefix Match (LPM)** algorithm (detailed below).
5.  **TTL Verification**: Inspects if the packet's Time-to-Live (TTL) has expired. If it has expired (`TTL <= 1`), the packet is dropped and handled via the `time_exceeded` ICMP protocol. Otherwise, the router decrements the TTL and incrementally updates the IP header checksum.
6.  **Next-Hop Resolution**: Searches for the destination MAC address of the next hop in the ARP table:
    * **Hit**: Updates the source and destination MAC fields in the Ethernet header and transmits the packet through the interface of the found route.
    * **Miss**: Adds the current packet to the packet retention queue and broadcasts an `arp_request`.


### 🌳 Efficient Longest Prefix Match (LPM) via Trie

To find the absolute best route efficiently, the routing table is structured as a **Trie (Prefix Tree)**, as suggested in the assignment description. Each node in the tree represents a single bit of the IP address, and route insertion is performed bit-by-bit into the tree depth.

#### The `insert` Function
* Parses the IP address bits in reverse order, traveling from the **Most Significant Bit (MSB)** down to the **Least Significant Bit (LSB)**.
* **Mask Check**: If the algorithm encounters a `0` bit in the network mask, the insertion process stops immediately.
* The router continues traversing the bits of the IP address. If no node exists at the current bit path, a new node is created using the `add_node()` helper function. If a node already exists, the algorithm simply descends deeper into the tree branch.

#### The `search` / `get_best_route` Function
* The search engine queries the trie to locate the best matching route.
* If it encounters a `NULL` node or completes the traversal of all address bits, the function returns the best current route found up to that point.
* Otherwise, it continuously updates the tracking reference to hold the best route of the current node and recursively calls itself, traversing the bits in reverse order.
* The tree is fully initialized inside `main` by looping through all entries of the input routing table.


### 🌐 Network Protocols

#### Module: `ARP Protocol`
The dynamic ARP protocol is implemented by removing the static `arp_table.txt` and using three key functions:

* **`send_arp_reply`**: Triggered upon receiving an `ARP Request` (`Opcode 1`). It copies the packet into a new buffer, extracts the headers, and swaps the Ethernet source/destination addresses. Within the ARP header, it sets `opcode` to `2` (Reply), sets the sender hardware/protocol addresses to the router's current interface MAC/IP, and maps the target hardware address to the initial sender's MAC.
* **`send_arp_request`**: Executed when the next-hop MAC is unknown. It creates a new packet from scratch (`0`-initialized buffer), extracts the Ethernet and ARP headers, and configures fields strictly according to **RFC 5342**.
* **`add_in_queue`**: Manages buffered packets to keep the router non-blocking. Triggered upon receiving an `ARP Reply` (`Opcode 2`), it inserts the new entry into the ARP table and creates a temporary queue. It loops through the main queue; if a packet's `next_hop` matches the new IP, its Ethernet header is updated and the packet is transmitted. Unmatched packets go into the temporary queue, and at the end, all temporary entries are moved back into the primary queue.


### 🛠️ ICMP Protocol
The engine handles diagnostics and error reporting through three custom ICMP message types:

* **`send_echo_reply`**: Responds to ICMP requests addressed directly to the router. It copies the packet into a new buffer, swaps the source/destination addresses for both Ethernet and IP layers, resets the TTL to a default boundary (`100`), modifies the ICMP `type` and `code` to `0`, and finally recalculates the IP checksum.
* **`send_destination_unreachable`**: Triggered when no valid route exists. It modifies the three base headers similarly to an echo reply, extracts the first `8 bytes (64 bits)` of the original packet's payload, updates the ICMP `type` and `code` to signal an unreachable destination, and recalculates both the IP checksum and total length fields.
* **`send_time_exceeded`**: Triggered when a packet's TTL expires (`TTL < 1`). It follows the exact same structural logic and payload encapsulation as `send_destination_unreachable`, but configures the specific ICMP `type` and `code` variables dedicated to time-outs.


### 📊 Functional Analysis

#### Subject 1: Standard Routing & Forwarding (`Subiectul1.png`)
Demonstrates the steady-state packet forwarding process. `Host1`, `Host2`, and `Host3` send active `ping` streams and successfully receive replies inside `router1` (`"We recieved a packet!"`), which explicitly confirms that the forwarding functionality works and packets safely reach their destination. All processed packets are successfully captured and tracked live inside Wireshark.

#### Subject 2: Dynamic Address Resolution (`Subiectul2.png`)
Demonstrates the live operational execution of the dynamic ARP protocol. The router processes incoming packets, but for certain nodes, it cannot find the corresponding MAC address. (For debugging purposes, tracking logs were initially printed inside `send_arp_reply` and `add_in_queue`, which were subsequently removed). The Wireshark trace captures the full exchange of request-reply packets: `ARP_Request` queries who owns the target destination IP, and `ARP_Reply` responds with the valid hardware address. Other background packets are processed normally via standard ICMP streams.

#### Subject 3: ICMP Diagnostic Validation (`Subiectul3.png`)
Validates edge-case ICMP error generation, which was explicitly tested and analyzed across three isolated hosts using a static ARP configuration:
* **Case 1 (Executed on Host2)**: Tests `send_echo_reply`. The host triggers a standard `ping` command, receives a valid echo response, and logs a stable `0% packet loss` feedback loop.
* **Case 2 (Executed on Host0)**: Tests `send_time_exceeded`. A specialized ping command is forced with a constraint of `ttl = 1`. The router evaluates the packet in the corresponding time-to-live conditional block, drops the packet, and issues the `"TTL Terminated!"` ICMP error report.
* **Case 3 (Executed on Host1)**: Tests `send_destination_unreachable`. A ping is directed toward an invalid IP address that does not exist anywhere inside the routing table. The router catches the lookup failure and outputs a `"Destination Unreachable!"` alert.
