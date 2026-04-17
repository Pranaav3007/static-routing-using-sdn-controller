# SDN Mininet Simulation Report

## Title

Static Routing using SDN Controller Installed Flow Rules

## Objective

The objective of this project is to implement static routing in a software-defined network using Mininet and a Ryu controller. The controller is responsible for reacting to `packet_in` events, selecting predefined forwarding paths, installing OpenFlow rules manually, validating traffic delivery, and proving that the chosen path remains unchanged after the rules are reinstalled.

## Problem Understanding

Traditional forwarding can adapt dynamically or rely on local switch decisions. In this project, the forwarding path is intentionally controlled from the SDN controller. This demonstrates how a centralized controller can define explicit routes, enforce policy decisions, and keep forwarding behavior predictable.

The assignment specifically requires:

- Mininet topology design
- controller-switch interaction
- explicit match-action flow rules
- at least two functional test scenarios
- validation or regression testing

## Topology and Design Choice

The chosen topology is a diamond with four OpenFlow switches and four hosts.

```text
h1 -- s1 -- s2 -- s4 -- h2
        \         /
         \-- s3 --/

h3 attached to s2
h4 attached to s3
```

This topology was selected because it contains both:

- a preferred static path for approved traffic
- an alternate physical path that is intentionally not used

That makes it easy to show routing control and to verify that reinstalling rules does not change the path.

## Static Routing Policy

Approved flows:

- `10.0.0.1 -> 10.0.0.2`: `s1 -> s2 -> s4`
- `10.0.0.2 -> 10.0.0.1`: `s4 -> s2 -> s1`
- `10.0.0.3 -> 10.0.0.4`: `s2 -> s1 -> s3`
- `10.0.0.4 -> 10.0.0.3`: `s3 -> s1 -> s2`

Disallowed flows:

- every source-destination combination that does not appear in the approved policy table

## Controller Logic

The Ryu controller performs the following tasks:

1. Install a table-miss rule on each switch so unmatched packets are sent to the controller.
2. Handle ARP requests with proxy ARP for approved destinations.
3. On the first IPv4 packet for an approved pair, compute the exact path and install flow entries on every switch in that path.
4. Install a drop rule when the traffic pair is not permitted by the static routing policy.
5. Reinstall the same route whenever the same flow needs to be installed again.

## Match-Action Design

For approved IPv4 flows, the rule match includes:

- `in_port`
- `eth_type = 0x0800`
- `ipv4_src`
- `ipv4_dst`

Action:

- output to the next hop port defined by the static path

For blocked flows, the controller installs a higher-priority match with no actions, which behaves as a drop rule.

## Functional Demonstration

### Scenario 1: Allowed Traffic

Command examples:

```bash
mininet> h1 ping -c 3 h2
mininet> h1 iperf -s -D
mininet> h2 iperf -c 10.0.0.1 -t 5
```

Expected behavior:

- `ping` succeeds because the controller installs the static path
- `iperf` shows end-to-end throughput
- `ovs-ofctl dump-flows` shows rules on `s1`, `s2`, and `s4`

### Scenario 2: Blocked Traffic

Command:

```bash
mininet> h1 ping -c 2 h4
```

Expected behavior:

- the ping fails
- controller log indicates the pair is blocked
- ingress switch gets a drop rule for the blocked pair

## Performance Observation and Analysis

The project observes the following metrics:

- latency using `ping`
- throughput using `iperf`
- flow-table state using `ovs-ofctl dump-flows`
- controller decision logs from `ryu-manager`

Interpretation:

- latency confirms reachability and general path health
- throughput shows that traffic is successfully forwarded end to end
- flow-table inspection confirms the path was installed exactly as intended
- logs demonstrate the controller's decision process and policy enforcement

## Regression Validation

The regression requirement is to ensure the path remains unchanged after rule reinstall.

Validation approach:

1. Trigger `h1 -> h2` once and capture flow tables.
2. Delete the three forwarding rules for `10.0.0.1 -> 10.0.0.2`.
3. Send traffic again from `h1` to `h2`.
4. Dump the flow tables again.
5. Compare output ports for each switch.

Expected result:

- `s1` still forwards from port `1` to port `2`
- `s2` still forwards from port `2` to port `3`
- `s4` still forwards from port `2` to port `1`

The repository also includes a Python unit test that verifies the computed flow signature is identical across repeated installations.

## Files Included

- `controller/static_routing_controller.py`
- `controller/routing_policy.py`
- `topology/static_routing_topology.py`
- `tests/test_routing_policy.py`
- `scripts/run_demo.sh`
- `scripts/collect_flow_dumps.sh`
- `README.md`
- `screenshots/`
- `validation/`

## Proof of Execution

This Windows workspace does not provide Mininet or Open vSwitch, so the live packet forwarding screenshots must be captured on the Ubuntu demo machine. The repository is already prepared for that final step:

- keep the generated submission screenshots in `screenshots/`
- keep raw validation text logs in `validation/`
- add the final Mininet and Wireshark screenshots to `screenshots/` using the next numbered names

## References

1. Ryu documentation: https://ryu.readthedocs.io/
2. Mininet documentation: https://mininet.org/
3. Open vSwitch manual pages: https://docs.openvswitch.org/
4. OpenFlow Switch Specification 1.3.1
