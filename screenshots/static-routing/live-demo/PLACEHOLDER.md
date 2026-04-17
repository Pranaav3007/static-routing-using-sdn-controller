# Live Demo Capture Checklist

Capture the final screenshots on the Ubuntu Mininet machine after launching the topology and controller.

Required captures:

- Mininet startup with switches connected to the Ryu controller
- `h1 ping -c 3 h2` success
- `h1 ping -c 2 h4` failure
- `h2 iperf -c 10.0.0.1 -t 5`
- `ovs-ofctl -O OpenFlow13 dump-flows s1`
- `ovs-ofctl -O OpenFlow13 dump-flows s2`
- `ovs-ofctl -O OpenFlow13 dump-flows s4`
- controller log showing route installation
- flow dumps before and after rule reinstall
- optional Wireshark capture on one switch link for proof of packet forwarding
