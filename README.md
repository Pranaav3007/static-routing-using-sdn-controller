# Static Routing using SDN Controller

This repository contains an SDN Mininet simulation project that demonstrates static routing using controller-installed OpenFlow rules. The implementation uses a custom Mininet topology and a Ryu controller that reacts to `packet_in` events, chooses predefined paths, installs exact match-action rules, and preserves the same path after rule reinstallation.

## Problem Statement

The objective is to implement deterministic forwarding paths in an SDN network. Instead of letting switches learn paths dynamically, the controller defines the route for each approved flow and installs the rules hop by hop. The project must show:

- controller-switch interaction
- explicit flow rule design
- packet delivery validation
- observable network behavior
- regression validation showing that the selected path does not change after flow reinstall

## Repository Layout

- `controller/static_routing_controller.py`: Ryu controller application
- `controller/routing_policy.py`: path definitions and flow-plan builder
- `topology/static_routing_topology.py`: Mininet diamond topology
- `tests/test_routing_policy.py`: route and regression tests
- `scripts/run_demo.sh`: helper to launch the controller and Mininet
- `scripts/collect_flow_dumps.sh`: helper to save switch flow tables
- `REPORT.md`: full report for submission
- `screenshots/`: screenshot images with submission-ready naming
- `validation/`: text logs used to generate validation screenshots

## Topology

The project uses a diamond topology so that a primary route and a physically available alternate route both exist.

```text
h1 -- s1 -- s2 -- s4 -- h2
        \         /
         \-- s3 --/

h3 is attached to s2
h4 is attached to s3
```

### Static Paths

- `h1 -> h2` uses `s1 -> s2 -> s4`
- `h2 -> h1` uses `s4 -> s2 -> s1`
- `h3 -> h4` uses `s2 -> s1 -> s3`
- `h4 -> h3` uses `s3 -> s1 -> s2`
- all other host pairs are blocked by policy

The alternate physical path `s1 -> s3 -> s4` exists, but it is intentionally not chosen for `h1 <-> h2`. That makes path verification and regression testing straightforward.

## How It Works

1. Each switch installs a table-miss rule that sends unknown packets to the controller.
2. On an ARP request for an approved destination, the controller replies with proxy ARP.
3. On the first IPv4 packet for an approved pair, the controller computes the predefined route and installs hop-by-hop OpenFlow rules on every switch in that path.
4. If the source-destination pair is not allowed, the controller installs a drop rule on the ingress switch.
5. Repeating the installation for the same pair produces the same flow signature, which is covered by the regression tests.

## Setup

This project is intended for **Ubuntu 22.04+** or another Linux environment with Mininet and Open vSwitch.

### System Dependencies

```bash
sudo apt update
sudo apt install -y mininet openvswitch-switch iperf3 wireshark python3-pip
python3 -m pip install --upgrade pip
python3 -m pip install -r requirements.txt
```

### Start the Demo

Terminal 1:

```bash
ryu-manager controller/static_routing_controller.py
```

Terminal 2:

```bash
sudo mn -c
sudo mn --custom topology/static_routing_topology.py \
  --topo staticrouting \
  --controller remote,ip=127.0.0.1,port=6633 \
  --switch ovsk,protocols=OpenFlow13 \
  --mac
```

## Validation Scenarios

### Scenario 1: Allowed Traffic

```bash
mininet> h1 ping -c 3 h2
mininet> h1 iperf -s -D
mininet> h2 iperf -c 10.0.0.1 -t 5
```

Expected result:

- ping succeeds
- throughput is reported by `iperf`
- flow tables on `s1`, `s2`, and `s4` show the installed route for `10.0.0.1 -> 10.0.0.2`

### Scenario 2: Blocked Traffic

```bash
mininet> h1 ping -c 2 h4
```

Expected result:

- ping fails
- controller log shows that the pair is not in the static policy
- ingress switch receives a drop rule for that source-destination pair

### Scenario 3: Regression After Rule Reinstall

1. Trigger the `h1 -> h2` path once.
2. Delete only the corresponding forwarding rules from `s1`, `s2`, and `s4`.
3. Ping again from `h1` to `h2`.
4. Dump flows and verify the same output ports are used.

Example delete commands:

```bash
sudo ovs-ofctl -O OpenFlow13 --strict del-flows s1 "priority=300,ip,in_port=1,nw_src=10.0.0.1,nw_dst=10.0.0.2"
sudo ovs-ofctl -O OpenFlow13 --strict del-flows s2 "priority=300,ip,in_port=2,nw_src=10.0.0.1,nw_dst=10.0.0.2"
sudo ovs-ofctl -O OpenFlow13 --strict del-flows s4 "priority=300,ip,in_port=2,nw_src=10.0.0.1,nw_dst=10.0.0.2"
```

The controller will reinstall the same path because the routing policy is deterministic.

## Local Tests

The Windows workspace used to prepare this repo does not include Mininet or Ryu runtime support, but the deterministic routing logic is still validated with pure Python tests:

```bash
py -m unittest tests.test_routing_policy -v
```

These tests verify:

- approved path selection
- blocked-pair rejection
- exact hop/port plan generation
- regression stability after route reinstall

## Screenshots and Proof

All screenshots are stored directly under `screenshots/` with numbered names so they are easy to submit and review.

- `01-readme-page-*.png`: README screenshots
- `02-report-page-*.png`: report screenshots
- `03-routing-policy-page-*.png`: routing policy code screenshots
- `04-static-routing-controller-page-*.png`: controller code screenshots
- `05-topology-page*.png`: Mininet topology code screenshots
- `06-routing-test-page*.png`: regression test code screenshot
- `07-routing-test-log*.png`: unit test output screenshot
- `08-compileall-log*.png`: compile validation output screenshot
- `09-live-demo-checklist*.png`: checklist of live Mininet screenshots to capture on Ubuntu

## References

- Ryu SDN Framework documentation
- Mininet walkthrough and API documentation
- Open vSwitch `ovs-ofctl` manual
