"""Mininet topology for deterministic SDN static routing."""

from mininet.topo import Topo


class StaticRoutingTopo(Topo):
    def build(self):
        h1 = self.addHost("h1", ip="10.0.0.1/24", mac="00:00:00:00:00:01")
        h2 = self.addHost("h2", ip="10.0.0.2/24", mac="00:00:00:00:00:02")
        h3 = self.addHost("h3", ip="10.0.0.3/24", mac="00:00:00:00:00:03")
        h4 = self.addHost("h4", ip="10.0.0.4/24", mac="00:00:00:00:00:04")

        s1 = self.addSwitch("s1", protocols="OpenFlow13")
        s2 = self.addSwitch("s2", protocols="OpenFlow13")
        s3 = self.addSwitch("s3", protocols="OpenFlow13")
        s4 = self.addSwitch("s4", protocols="OpenFlow13")

        self.addLink(h1, s1, port2=1)
        self.addLink(h3, s2, port2=1)
        self.addLink(h4, s3, port2=1)
        self.addLink(h2, s4, port2=1)

        self.addLink(s1, s2, port1=2, port2=2)
        self.addLink(s1, s3, port1=3, port2=2)
        self.addLink(s2, s4, port1=3, port2=2)
        self.addLink(s3, s4, port1=3, port2=3)


topos = {"staticrouting": StaticRoutingTopo}
