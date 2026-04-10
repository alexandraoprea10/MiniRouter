#include <arpa/inet.h> /* ntoh, hton and inet_ functions */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "lib.h"
#include "protocols.h"
#include <string.h>

/* Routing table */
struct route_table_entry *rtable;
int rtable_len;

/* Arp table */
struct arp_table_entry *mac_table;
int mac_table_len;

/*
 Returns a pointer (eg. &rtable[i]) to the best matching route, or NULL if there
 is no matching route.
*/
struct route_table_entry *get_best_route(uint32_t ip_dest) {
	/* TODO 2.2: Implement the LPM algorithm */
	/* We can iterate through rtable for (int i = 0; i < rtable_len; i++). Entries in
	 * the rtable are in network order already */
	struct route_table_entry *newEntry = NULL;
	for (int i = 0; i < rtable_len; i++) {
    if (rtable[i].prefix == (ip_dest & rtable[i].mask)) {
      if (newEntry == NULL)
		newEntry = &rtable[i];
	if (ntohl(rtable[i].mask) > ntohl(newEntry->mask))
		newEntry = &rtable[i];
    }
}
	return newEntry;
}

struct arp_table_entry *get_mac_entry(uint32_t given_ip) {
	/* TODO 2.4: Iterate through the MAC table and search for an entry
	 * that matches given_ip. */

	/* We can iterate thrpigh the mac_table for (int i = 0; i <
	 * mac_table_len; i++) */
	for (int i = 0 ; i < mac_table_len; i++) {
		if (mac_table[i].ip == given_ip)
		return &mac_table[i];
	}
	return NULL;
}

void send_echo_reply(int packet_len, char *packet, int interface) {
	// iau headerele din pachetul curent
	struct ether_hdr *current_eth = (struct ether_hdr *)packet;
	struct ip_hdr *current_ip = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));
	struct icmp_hdr *current_icmp = (struct icmp_hdr *)(packet + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));

	// copiez in buffer pachetul curent pentru ca urmeaza sa il modific
	char buf[1500];
	// retine informatiile pe care nu le reactualizez
	memcpy(buf, packet, packet_len);

	// iau headerele din pachetul nou
	struct ether_hdr *new_eth = (struct ether_hdr *)buf;
	struct ip_hdr *new_ip = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));
	struct icmp_hdr *new_icmp = (struct icmp_hdr *)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));

	// pentru Ethernet
	// schimb destinatarul
	memcpy(new_eth->ethr_dhost, current_eth->ethr_shost, 6);
	uint8_t macAddress[6];
	get_interface_mac(interface, macAddress);
	// sursa devine mac-ul interfetei pe care o voi trimite
	memcpy(new_eth->ethr_shost, macAddress, 6);

	// pentru ip
	// recalculez checksum-ul cu noul ip
	new_ip->checksum = 0;
	new_ip->checksum = htons(checksum((uint16_t *)new_ip, sizeof(struct ip_hdr)));
	// destinatia devine sursa
	new_ip->dest_addr = current_ip->source_addr;
	// new_ip->frag = se copiaza din pachetul curent
	// new_ip->id = se copiaza din pachetul curent
	// new_ip->ihl = se copiaza din pachetul curent
	// new_ip->proto = se copiaza din pachetul curent
	// sursa devine ip-ul interfetei noastre
	new_ip->source_addr = inet_addr(get_interface_ip(interface));
	// new_ip->tos = se copiaza din pachetul curent
	// new_ip->tot_len = se copiaza din pachetul curent
	// resetez ttl-ul
	new_ip->ttl = 100;
	// new_ip->ver = se copiaza din pachetul curent

	// pentru icmp
	// echo_reply are codul (0,0)
	new_icmp->check = 0;
	new_icmp->mcode = 0;
	new_icmp->mtype = 0;
	// new_icmp->un_t = 0;

	// recalculez checksum pentru icmp
	// mai intai recalculez lungimea
	// int ip_length = new_ip->ihl * 4;
	// int icmp_length = (new_ip->tot_len) - ip_length;
	new_icmp->check = htons(checksum((uint16_t *)new_icmp, sizeof(struct ip_hdr)));
	send_to_link(packet_len, buf, interface);
}

int main(int argc, char *argv[])
{
	int interface;
	char packet[1500];
	int packet_len;

	/* Don't touch this */
	init(argv + 2, argc - 2);

	/* Code to allocate the MAC and route tables */
	rtable = malloc(sizeof(struct route_table_entry) * 1000000);
	/* DIE is a macro for sanity checks */
	DIE(rtable == NULL, "memory");

	mac_table = malloc(sizeof(struct  arp_table_entry) * 1000000);
	DIE(mac_table == NULL, "memory");
	
	/* Read the static routing table and the MAC table */
	rtable_len = read_rtable(argv[1], rtable);

	mac_table_len = parse_arp_table("arp_table.txt", mac_table);

	while (1) {
		/* We call get_packet to receive a packet. get_packet returns
		the interface it has received the data from. And writes to
		len the size of the packet. */
		interface = recv_from_any_link(packet, (size_t *)&packet_len);
		DIE(interface < 0, "get_message");
		printf("We have received a packet\n");
		
		/* Extract the Ethernet header from the packet. Since protocols are
		 * stacked, the first header is the ethernet header, the next header is
		 * at m.payload + sizeof(struct ether_header) */
		struct ether_hdr *eth_hdr = (struct ether_hdr *) packet;
		struct ip_hdr *ip_header = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));

		/* Check if we got an IPv4 packet */
		if (eth_hdr->ethr_type != ntohs(ETHERTYPE_IP)) {
			printf("Ignored non-IPv4 packet\n");
			continue;
		}

		/* TODO 2.1: Check the ip_hdr integrity using ip_checksum((uint16_t *)ip_hdr, sizeof(struct iphdr)) */
		if (checksum((uint16_t *)ip_header, sizeof(struct ip_hdr)) != 0) {
			printf("Wrong IP!\n");
			continue;
		}
		// verificam daca pachetul este de tip ICMP
		if (ip_header->proto == 1) {
				struct icmp_hdr *new_icmp = (struct icmp_hdr *)(packet + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
				if (new_icmp->mtype == 8 && new_icmp->mcode == 0) {
					printf("Transmitting ICMP Echo Request!\n");
					send_echo_reply(packet_len, packet, interface);
				}
			continue;
		}
		/* TODO 2.2: Call get_best_route to find the most specific route, continue; (drop) if null */
		struct route_table_entry *bestRoute = get_best_route(ip_header->dest_addr);
		if (bestRoute == NULL) {
			printf("Route not found!\n");
			// send_icmp_bad(interface, packet, 3, 0);
			continue;
		}
		/* TODO 2.3: Check TTL > 1. Update TLL. Update checksum  */
		if (ip_header->ttl <= 1) {
			printf("TTL terminated!\n");
			continue;
		}
		ip_header->ttl--;
		ip_header->checksum = 0;
		// am modificat checksum pentru ca reteaua foloseste big-endian(network-order)
		// pe cand calculatorul foloseste little-endian(host-order)
		ip_header->checksum = htons(checksum((uint16_t *)ip_header, sizeof(struct ip_hdr)));
		/* TODO 2.4: Update the ethernet addresses. Use get_mac_entry to find the destination MAC
		 * address. Use get_interface_mac(m.interface, uint8_t *mac) to
		 * find the mac address of our interface. */
		struct arp_table_entry *mac = get_mac_entry(bestRoute->next_hop);
		if (mac == NULL) {
			printf("Wrong MAC!\n");
			continue;
		}
		memcpy(eth_hdr->ethr_dhost, mac->mac, 6);

		uint8_t macAddress[6];
		get_interface_mac(bestRoute->interface, macAddress);
		memcpy(eth_hdr->ethr_shost, macAddress, 6);
		// Call send_to_link(best_router->interface, packet, packet_len);
		send_to_link(packet_len, packet, bestRoute->interface);
	}
}